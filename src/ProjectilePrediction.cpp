#include "PCH.h"
#include "ProjectilePrediction.h"
#include "Ballistics.h"
#include "Utils.h"
#include "TrueHUDArrowTrail.h"
#include "Raycast.h"
#include "PredictorTelemetry.h"

thread_local bool g_nativeTeardownRequested = false;

struct alignas( 16 ) SimpleShapePhantomCInfo
{
	uint32_t collisionFilterInfo;  // 00
	uint32_t unk04;                // 04
	RE::hkpShape* shape;           // 08
	uint8_t unk10;                 // 10
	byte pad11[0x07];              // 11
	uint64_t unk18;                // 18
	uint32_t unk20;                // 20
	uint32_t unk24;                // 24 = 0x80000000
	uint64_t unk28;                // 28
	RE::hkTransform transform;     // 30
};

static_assert( sizeof( SimpleShapePhantomCInfo ) == 0x70 );
static_assert( offsetof( SimpleShapePhantomCInfo, shape ) == 0x08 );
static_assert( offsetof( SimpleShapePhantomCInfo, unk24 ) == 0x24 );
static_assert( offsetof( SimpleShapePhantomCInfo, transform ) == 0x30 );

template<typename T>
static T* ConstructProjectile( size_t a_runtimeSize, const RE::Projectile::LaunchData& a_launchData, REL::ID a_ctor )
{
	T* projectile = RE::malloc<T>( a_runtimeSize );

	using func_t = T*( T*, const RE::Projectile::LaunchData& );
	auto func = REL::Relocation<func_t>( a_ctor );

	auto result = func( projectile, a_launchData );
	return result;
}

RE::Projectile* Construct( const RE::Projectile::LaunchData& a_launchData )
{
	if( !a_launchData.projectileBase )
		return nullptr;

	static REL::ID ctorArrow{	RELOCATION_ID( 42532, 43695 ) };
	static REL::ID ctorBeam{	RELOCATION_ID( 42583, 43746 ) };
	static REL::ID ctorCone{	RELOCATION_ID( 42620, 43785 ) };
	static REL::ID ctorGrenade{ RELOCATION_ID( 42744, 43911 ) };
	static REL::ID ctorMissile{ RELOCATION_ID( 42848, 44023 ) };

	bool pre629 = Runtime::version < SKSE::RUNTIME_SSE_1_6_629;
	switch( a_launchData.projectileBase->data.types.get() )
	{
	case RE::BGSProjectileData::Type::kArrow:
		return ConstructProjectile<RE::ArrowProjectile>( pre629 ? 0x1F0 : 0x1F8, a_launchData, ctorArrow );

	case RE::BGSProjectileData::Type::kBeam:
		return ConstructProjectile<RE::BeamProjectile>( pre629 ? 0x240 : 0x248, a_launchData, ctorBeam );

	case RE::BGSProjectileData::Type::kCone:
		return ConstructProjectile<RE::ConeProjectile>( pre629 ? 0x218 : 0x220, a_launchData, ctorCone );

	case RE::BGSProjectileData::Type::kGrenade:
		return ConstructProjectile<RE::GrenadeProjectile>( pre629 ? 0x1E8 : 0x1F0, a_launchData, ctorGrenade );

	case RE::BGSProjectileData::Type::kMissile:
		return ConstructProjectile<RE::MissileProjectile>( pre629 ? 0x1E0 : 0x1E8, a_launchData, ctorMissile );

	default:
		return nullptr;
	}
}

RE::bhkSimpleShapePhantom* ConstructPhantom( RE::Projectile* a_projectile, RE::CFilter a_collisionFilter )
{
	RE::bhkShapePhantom* wrapper = (RE::bhkShapePhantom*)RE::malloc( sizeof( RE::bhkShapePhantom ) );

	using func_t = void( RE::bhkShapePhantom* );
	static REL::Relocation<func_t> func{ RELOCATION_ID( 75965, 77791 ) };
	func( wrapper );

	SimpleShapePhantomCInfo cinfo{};

	cinfo.collisionFilterInfo = a_collisionFilter.filter;

	cinfo.shape = static_cast<RE::hkpShape*>( a_projectile->GetCollisionShape()->referencedObject.get() );
	cinfo.unk10 = 2;
	cinfo.unk24 = 0x80000000;
	cinfo.transform.rotation.col0 = RE::hkVector4{ 1.0f, 0.0f, 0.0f, 0.0f };
	cinfo.transform.rotation.col1 = RE::hkVector4{ 0.0f, 1.0f, 0.0f, 0.0f };
	cinfo.transform.rotation.col2 = RE::hkVector4{ 0.0f, 0.0f, 1.0f, 0.0f };
	cinfo.transform.translation = RE::hkVector4{ 0.0f, 0.0f, 0.0f, 0.0f };

	*reinterpret_cast<uintptr_t*>( wrapper ) = RE::VTABLE_bhkSimpleShapePhantom[ 0 ].address();

	using inner_func_t = void( RE::bhkShapePhantom*, SimpleShapePhantomCInfo* );
	static REL::Relocation<inner_func_t> func_inner{ RELOCATION_ID( 76907, 78783 ) };
	func_inner( wrapper, &cinfo );

	wrapper->unk28 = false;

	return (RE::bhkSimpleShapePhantom*)wrapper;
}

void PreparePhantom( RE::bhkSimpleShapePhantom* a_phantom, const RE::Projectile::LaunchData& a_launchData )
{
	using get_havok_world_t = RE::hkpWorld* ( RE::TESObjectCELL* );
	static REL::Relocation<get_havok_world_t> getHavokWorld{ RELOCATION_ID( 18536, 18995 ) };
	auto havokWorld = getHavokWorld( a_launchData.shooter->GetParentCell() );

	using attach_to_world_t = void( RE::bhkSimpleShapePhantom*, RE::hkpWorld* );
	auto** vtable = *reinterpret_cast<void***>( a_phantom );
	auto attachToWorld = reinterpret_cast<attach_to_world_t*>( vtable[ 0x29 ] );
	attachToWorld( a_phantom, havokWorld );

	using func_t = void( RE::bhkSimpleShapePhantom*, const RE::hkVector4& );
	static REL::Relocation<func_t> setPosition{ RELOCATION_ID( 76895, 78771 ) };
	setPosition( a_phantom, a_launchData.origin * havokScaling );
}

RE::Projectile* CreatePredictor( const RE::Projectile::LaunchData& a_launchData, RE::CFilter a_collisionFilter )
{
	auto predictor = Construct( a_launchData );
	if( !predictor )
		return nullptr;

	auto& runtimeData = predictor->GetProjectileRuntimeData();
	runtimeData.flags.set( RE::Projectile::Flags::kNoDamageOutsideCombat );
	runtimeData.flags.reset(
		RE::Projectile::Flags::kAddedToManager,
		RE::Projectile::Flags::kProcessedImpacts,
		RE::Projectile::Flags::kDestroyed,
		RE::Projectile::Flags::kMoved
	);

	// Create phantom
	auto phantom = ConstructPhantom( predictor, a_collisionFilter );

	// unk0E0 is the phantom that's used by predictor step
	runtimeData.unk0E0 = phantom;

	PreparePhantom( phantom, a_launchData );

	return predictor;
}

void DeletePredictor( RE::Projectile* a_predictor )
{
	if( !a_predictor )
		return;

	ProjectilePrediction::PredictorTelemetry::GetSingleton()->RecordPredictorDestruction();

	auto& predictorRuntime = a_predictor->GetProjectileRuntimeData();
	// Cleanup
	if( !predictorRuntime.flags.any(RE::Projectile::Flags::kDestroyed) )
	{
		using destroy_t = void( RE::Projectile* );
		static REL::Relocation<destroy_t> destroyProjectile{ RELOCATION_ID( 42930, 44110 ) };
		destroyProjectile( a_predictor );
	}

	assert( predictorRuntime.flags.any( RE::Projectile::Flags::kDestroyed ) );

	RE::GarbageCollector::GetSingleton()->Add( a_predictor, true );
}

bool ResetPredictor( RE::Projectile* a_predictor, const RE::Projectile::LaunchData& a_launchData, const RE::NiPoint3& a_origin )
{
	if( !a_predictor )
		return false;

	a_predictor->SetPosition( a_origin );

	auto& runtime = a_predictor->GetProjectileRuntimeData();

	runtime.actorCause.reset();

	runtime.shooter = a_launchData.shooter;
	runtime.desiredTarget = a_launchData.desiredTarget;

	runtime.weaponSource = a_launchData.weaponSource;
	runtime.ammoSource = a_launchData.ammoSource;
	runtime.spell = a_launchData.spell;

	runtime.castingSource = a_launchData.castingSource;

	runtime.power = a_launchData.power;
	runtime.speedMult = 1.0f;

	runtime.range = std::max( 60000.0f, a_launchData.projectileBase->data.range );

	runtime.livingTime = 0.0f;

	// Critical: a positive value enables the immediate character-hit dispatcher at SkyrimSE.exe+0x7EBFD1. 1.6.1170
	runtime.weaponDamage = predictorDamage;

	runtime.distanceMoved = 0.0f;
	runtime.scale = 1.0f;

	runtime.velocity = {};
	runtime.linearVelocity = {};

	runtime.flags.reset(
		RE::Projectile::Flags::kDestroyed,
		RE::Projectile::Flags::kMoved,
		RE::Projectile::Flags::kProcessedImpacts,
		RE::Projectile::Flags::kUseOrigin );

	runtime.flags.set(
		RE::Projectile::Flags::kNoDamageOutsideCombat,
		RE::Projectile::Flags::kAutoAim );

	//REX::DEBUG( "Predictor speed components: base={}, powerMult={}, weaponMult={}, projectileMult={}",
	//	a_launchData.projectileBase->data.speed,
	//	a_predictor->GetPowerSpeedMult(),
	//	a_predictor->GetWeaponSpeedMult(),
	//	runtime.speedMult );

	runtime.linearVelocity.x =
		a_launchData.projectileBase->data.speed *
		a_predictor->GetPowerSpeedMult() *
		a_predictor->GetWeaponSpeedMult() *
		runtime.speedMult;

	Ballistics::SetProjectileAngle( a_predictor, a_launchData.angleX, a_launchData.angleZ );

	// Clear impacts through the correct container ownership path.
	// Do not blindly memset the impact collection.
	runtime.impacts.clear();

	auto phantom = (RE::bhkSimpleShapePhantom*)runtime.unk0E0;

	if( !phantom )
	{
		ProjectilePrediction::PredictorTelemetry::GetSingleton()->RecordResetFailure();
		return false;
	}

	if( !phantom->phantom.get()->world )
	{
		// Attempt to re-init the phantom
		PreparePhantom( phantom, a_launchData );
	}

	using set_position_t = void( RE::bhkSimpleShapePhantom*, const RE::hkVector4& );
	static REL::Relocation<set_position_t> setPosition{ RELOCATION_ID( 76895, 78771 ) };

	setPosition( phantom, a_origin * havokScaling );

	return true;
}

void ProjectilePrediction::CleanUpPredictorCache()
{
	PredictorPoolManager::GetSingleton()->Destroy();
}

ProjectilePrediction::PredictorPoolManager* ProjectilePrediction::PredictorPoolManager::GetSingleton()
{
	static PredictorPoolManager singleton;
	return &singleton;
}

ProjectilePrediction::PredictorPool* ProjectilePrediction::PredictorPoolManager::GetPredictors( const RE::Projectile::LaunchData& a_launchData, RE::CFilter a_collisionFilter, const RE::NiPoint3* a_offsets )
{
	std::lock_guard lock{ poolMutex };

	// Check free pool
	size_t matchIdx = SIZE_MAX;
	size_t freeSlotIdx = SIZE_MAX;
	for( auto i = 0; i < _poolCount; i++ )
	{
		if( !_predictorKey[ i ] && freeSlotIdx == SIZE_MAX )
			freeSlotIdx = i;

		if( _predictorKey[ i ] &&
			_predictorKey[ i ] == a_launchData.projectileBase &&
			!_inUse[ i ] )
		{
			matchIdx = i;
			break;
		}
	}

	// Return matched free pool
	if( matchIdx < _poolCount )
	{
		_inUse[ matchIdx ] = true;
		return &_predictorPool[ matchIdx ];
	}

	// Start from 0 if there's no free slot and replace the unused one
	if( freeSlotIdx == SIZE_MAX )
		freeSlotIdx = 0;

	// Try create the new pool if there's a free slot
	for( auto i = freeSlotIdx; i < _poolCount; i++ )
	{
		auto& pool = _predictorPool[ i ];
		// Destroy unused predictor before attemping to create new pool
		if( !_inUse[ i ] && _predictorKey[ i ] )
		{
			PredictorTelemetry::GetSingleton()->RecordPoolReplacement();

			for( auto j = 0; j < PredictorPool::kPredictorCount; j++ )
			{
				DeletePredictor( pool[ j ] );
				pool[ j ] = nullptr;
			}

			_predictorKey[ i ] = nullptr;
		}

		if( !_predictorKey[ i ] )
		{
			_predictorKey[ i ] = a_launchData.projectileBase;
			_inUse[ i ] = true;

			PredictorTelemetry::GetSingleton()->RecordPoolCreation();

			bool createFailed = false;
			for( auto j = 0; j < PredictorPool::kPredictorCount; j++ )
			{
				auto launchData = a_launchData;
				launchData.origin += a_offsets[ j ];
				auto predictor = CreatePredictor( launchData, a_collisionFilter );

				if( !predictor )
				{
					createFailed= true;
					break;
				}

				pool[ j ] = predictor;
			}

			if( createFailed )
			{
				_predictorKey[ i ] = nullptr;
				_inUse[ i ] = false;

				for( auto j = 0; j < PredictorPool::kPredictorCount; j++ )
				{
					DeletePredictor( pool[ j ] );
					pool[ j ] = nullptr;
				}
				ProjectilePrediction::PredictorTelemetry::GetSingleton()->RecordPoolCreationFailure();

				return nullptr;
			}

			return &pool;
		}
	}

	return nullptr;
}

void ProjectilePrediction::PredictorPoolManager::ReleasePredictor( PredictorPool* a_pool )
{
	std::lock_guard lock{ poolMutex };
	for( auto i = 0; i < _poolCount; i++ )
	{
		if( &_predictorPool[ i ] == a_pool )
		{
			assert( _inUse[ i ] );
			_inUse[ i ] = false;

			return;
		}
	}
}

void ProjectilePrediction::PredictorPoolManager::Destroy()
{
	std::lock_guard lock{ poolMutex };
	for( auto i = 0; i < _poolCount; i++ )
	{
		assert( !_inUse[ i ] );

		for( auto j = 0; j < PredictorPool::kPredictorCount; j++ )
		{
			if( _predictorPool[ i ][ j ] )
			{
				DeletePredictor( _predictorPool[ i ][ j ] );
				_predictorPool[ i ][ j ] = nullptr;
			}
		}

		_predictorKey[ i ] = nullptr;
		_inUse[ i ] = false;
	}
}

// This is the reimplementation of the game native VATS projectile simulator with unused functions pruned
ProjectilePredictionResult ProjectilePrediction::Predictor::Predict( const RE::Projectile::LaunchData& a_launchData )
{
#ifdef _DEBUG
	PredictorTelemetryLease telemetryLease;
#endif
	ProjectilePredictionResult result;

	float collisionRadius = a_launchData.projectileBase->data.collisionRadius;
	auto launchData = a_launchData;

	float sinX = sin( a_launchData.angleX );
	float cosX = cos( a_launchData.angleX );
	float sinZ = sin( a_launchData.angleZ );
	float cosZ = cos( a_launchData.angleZ );

	RE::NiPoint3 forward{
		sinZ * cosX,
		cosZ * cosX,
		-sinX
	};

	RE::NiPoint3 right = forward.Cross( { 0.0f, 0.0f, 1.0f });

	if( right.SqrLength() <= 1.0e-6f )
		right = { 1.0f, 0.0f, 0.0f };
	else
		right.Unitize();

	RE::NiPoint3 up = right.Cross( forward );
	up.Unitize();

	RE::NiPoint3 originOffsets[] = {
		{ 0, 0, 0 },
		right * collisionRadius,
		right * -collisionRadius,
		up * collisionRadius,
		up * -collisionRadius
	};

	PredictorPoolLease& predictors = _lease;
	// Only simulate if all predictors are successfully created
	if( predictors )
	{
		for( auto i = 0; i < PredictorPool::kPredictorCount; i++ )
		{
			auto predictor = (*predictors)[ i ];
			if( !ResetPredictor( predictor, a_launchData, a_launchData.origin + originOffsets[ i ] ) )
				return result;
		}

		using updateMovementAndCollision_t = void( RE::Projectile*, float );
		static REL::Relocation<updateMovementAndCollision_t> updateMovementAndCollision{ RELOCATION_ID( 43006, 44197 ) };

		if( Settings::bShowFlightPrediction )
			TrueHUDArrowTrail::BeginStaticTrail( (*predictors)[0]->GetPosition() );

		g_nativeTeardownRequested = false;

		float simulationStep = 1.0f / 30.0f; // 1/30 second time step
		float maxSimulationTime = 10.0f; // 10 seconds
		float simulatedTime = 0.0f;
		float currentTimeStep = 0.0001f;
		bool isFirstStep = true;
		bool shouldContinue = true;
		while( shouldContinue )
		{
			for( auto i = 0; i < PredictorPool::kPredictorCount; i++ )
			{
				auto predictor = (*predictors)[ i ];
				auto& predictorRuntime = predictor->GetProjectileRuntimeData();

				PredictorTelemetry::GetSingleton()->RecordPredictionStep();

				updateMovementAndCollision( predictor, currentTimeStep );

				if( !predictorRuntime.impacts.empty() )
				{
					result.type = ProjectilePredictionResult::kImpacted;
					result.impact = *predictorRuntime.impacts.front();
					shouldContinue = false;

					RE::NiPoint3 predictorLocation = predictor->GetPosition();

					if( i != 0 )
					{
						result.impact.desiredTargetLoc -= originOffsets[ i ];
						predictorLocation -= originOffsets[ i ];
					}

					RE::NiPoint3 impactDirection = -result.impact.negativeVelocity;
					float impactAdvance = (predictorLocation - result.impact.desiredTargetLoc).Length();

					if( impactDirection.Unitize() > 0.0001f )
						result.location = result.impact.desiredTargetLoc + impactDirection * impactAdvance;
					else
						result.location = result.impact.desiredTargetLoc;

					break;
				}

				if( predictorRuntime.flags.any( RE::Projectile::Flags::kDestroyed ) )
				{
					result.type = ProjectilePredictionResult::kDestroyed;
					shouldContinue = false;
					break;
				}

				if( predictorRuntime.distanceMoved > predictorRuntime.range )
				{
					result.type = ProjectilePredictionResult::kOutOfRange;
					shouldContinue = false;
					break;
				}

				if( simulatedTime >= maxSimulationTime )
				{
					result.type = ProjectilePredictionResult::kFlightTimeTooLong;
					shouldContinue = false;
					break;
				}
			}

			if( Settings::bShowFlightPrediction )
				TrueHUDArrowTrail::AppendTrailPosition( ((*predictors)[0])->GetPosition() );

			simulatedTime += currentTimeStep;
			if( isFirstStep )
			{
				isFirstStep = false;
				currentTimeStep = simulationStep;
			}
		}

		RE::Projectile* mainPredictor = (*predictors)[0];
		if( result.type != ProjectilePredictionResult::kImpacted )
		{
			result.location = mainPredictor->GetPosition();
		}

		result.distanceMoved = mainPredictor->GetProjectileRuntimeData().distanceMoved;
		result.flightTime = simulatedTime;
	}

	PredictorTelemetry::GetSingleton()->RecordPrediction( result );
	return result;
}
