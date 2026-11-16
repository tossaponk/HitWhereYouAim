#include "Raycast.h"
#include "Utils.h"
#include "Settings.h"
#include "TrueHUDArrowTrail.h"
#include "Crosshair.h"

struct CrosshairRaycastCache
{
	RE::NiPoint3		position;
	RE::bhkPickData		pickData;
	RE::TESObjectREFR*	target;
	RE::COL_LAYER		collisionLayer;
	uint32_t			collisionGroup;
	uint32_t			frameCreated;

	CrosshairRaycastCache()
	{
		memset( this, 0, sizeof(CrosshairRaycastCache) );
	}
};

namespace Raycast
{
	// Crosshair raycast position cache
	CrosshairRaycastCache g_crosshairCache;
	RE::NiPoint3 lastRayCoord = {0,0,0};
	uint32_t rayImpactExpire = 0;
}

float GetCameraRayRange( const RE::NiPoint3& a_cameraPosition, const RE::NiPoint3& a_cameraDirection, const RE::NiPoint3& a_rangeOrigin, float a_range )
{
	auto offset = a_cameraPosition - a_rangeOrigin;
	float projection = offset.Dot( a_cameraDirection );
	float discriminant = projection * projection + a_range * a_range - offset.SqrLength();

	if( discriminant <= 0.0f )
		return 0.0f;

	return -projection + sqrt( discriminant );
}

void PickDataRayInputCopy( RE::bhkPickData& a_dst, RE::bhkPickData& a_src, float a_havokRange )
{
	a_dst.rayInput = a_src.rayInput;

	if( a_havokRange != FLT_MAX )
	{
		auto rayDirection = a_dst.rayInput.to - a_dst.rayInput.from;
		auto length = rayDirection.Length3();
		if( length > 0.0f )
		{
			rayDirection = rayDirection / length;
			a_dst.ray = rayDirection * a_havokRange;
		}
		else
			a_dst.ray = {};
	}
	else
	{
		a_dst.ray = a_dst.rayInput.to - a_dst.rayInput.from;
	}
}

RE::TESObjectREFR* Raycast::GetTarget( const RE::NiPoint3& a_origin, const RE::NiPoint3& a_target, uint32_t a_collisionGroup, RE::COL_LAYER a_collisionLayer, RE::NiPoint3* out_hitLocation, RE::bhkPickData* out_pickData )
{
    RE::bhkPickData pickData;
    RE::CFilter filter{ 0 };
    memset( &pickData, 0, sizeof( pickData ) );
    filter.SetCollisionLayer( a_collisionLayer );
    filter.SetSystemGroup( a_collisionGroup );

    pickData.rayInput.from = a_origin * havokScaling;
    pickData.rayInput.to = a_target * havokScaling;
    pickData.rayInput.filterInfo = filter;

    auto target = RE::TES::GetSingleton()->Pick( pickData );
    if( out_pickData )
        *out_pickData = pickData;

    if( out_hitLocation )
    {
        RE::hkVector4 hitLocation = pickData.rayInput.from + ( pickData.rayInput.to - pickData.rayInput.from ) * pickData.rayOutput.hitFraction;
        hitLocation = hitLocation * ( 1.0f / havokScaling );

        *out_hitLocation = (RE::NiPoint3&)hitLocation.quad.m128_f32;
    }

    if( target )
        return GetObjectReferenceFromAVObject( target );

    return nullptr;
}

RE::TESObjectREFR* Raycast::GetScreenTarget( uint32_t a_x, uint32_t a_y, uint32_t a_collisionGroup, RE::COL_LAYER a_collisionLayer, RE::NiPoint3* out_hitLocation, RE::bhkPickData* out_pickData, float a_maxDistance )
{
	auto camera = RE::Main::WorldRootCamera();
	auto screenSize = RE::BSGraphics::Renderer::GetSingleton()->GetScreenSize();

	RE::NiPoint3 origin, direction;
	camera->WindowPointToRay( a_x, a_y, origin, direction, (float)screenSize.width, (float)screenSize.height );

	RE::NiPoint3 target = camera->world.translate + direction * a_maxDistance;
	return GetTarget( camera->world.translate, target, a_collisionGroup, a_collisionLayer, out_hitLocation, out_pickData );
}

RE::TESObjectREFR* Raycast::GetCrosshairTarget( uint32_t a_collisionGroup, RE::COL_LAYER a_collisionLayer, RE::NiPoint3* out_hitLocation, RE::bhkPickData* out_pickData )
{
	auto currentFrame = RE::BSGraphics::State::GetSingleton()->GetFrameCount();

	// Invalidate cache
	if( g_crosshairCache.frameCreated != currentFrame ||
		g_crosshairCache.collisionGroup != a_collisionGroup ||
		g_crosshairCache.collisionLayer != a_collisionLayer )
	{
		auto screenSize = RE::BSGraphics::Renderer::GetSingleton()->GetScreenSize();
		auto screenHalfWidth = screenSize.width / 2;
		auto screenHalfHeight = screenSize.height / 2;
		int32_t crosshairX = (int32_t)( screenHalfWidth + ( Settings::fCrosshairPosX * screenHalfWidth ) );
		int32_t crosshairY = (int32_t)( screenHalfHeight + ( Settings::fCrosshairPosY * screenHalfHeight ) );

		g_crosshairCache.target = Raycast::GetScreenTarget(
			crosshairX,
			crosshairY,
			a_collisionGroup,
			a_collisionLayer,
			&g_crosshairCache.position,
			&g_crosshairCache.pickData,
			Settings::fMaximumTargetDistance * meterToGameUnit );

		g_crosshairCache.frameCreated = currentFrame;
		g_crosshairCache.collisionGroup = a_collisionGroup;
		g_crosshairCache.collisionLayer = a_collisionLayer;
	}

	if( out_hitLocation ) *out_hitLocation = g_crosshairCache.position;
	if( out_pickData ) *out_pickData = g_crosshairCache.pickData;
	return g_crosshairCache.target;
}

RE::TESObjectREFR* Raycast::GetCrosshairTarget( const RE::CFilter& a_collisionFilter, RE::NiPoint3* out_hitLocation, RE::bhkPickData* out_pickData )
{
	return GetCrosshairTarget( a_collisionFilter.GetSystemGroup(), a_collisionFilter.GetCollisionLayer(), out_hitLocation, out_pickData );
}

RE::Actor* Raycast::ResolveRayTarget( RE::bhkPickData* a_pickData, RE::hkVector4 a_origin, bool a_useTargetActor )
{
	float range = a_pickData->ray.Length3();
	RE::NiPoint3 hitCrosshair{ 0, 0, 0 };
	RE::bhkPickData crosshairPickData;
	RE::Actor* crosshairTarget = nullptr;

	// Crosshair ray phase
	{
		auto pickTarget = GetCrosshairTarget( a_pickData->rayInput.filterInfo, &hitCrosshair, &crosshairPickData );

		if( Settings::bShowLastProjectilePath )
		{
			auto& rayStart = crosshairPickData.rayInput.from;
			auto rayEnd = crosshairPickData.rayInput.from + ( crosshairPickData.rayInput.to - crosshairPickData.rayInput.from ) * crosshairPickData.rayOutput.hitFraction;
			TrueHUDArrowTrail::BeginRay(
				HavokToWorld<RE::NiPoint3>( rayStart ),
				HavokToWorld<RE::NiPoint3>( rayEnd ) );
		}

		if( pickTarget )
			crosshairTarget = pickTarget->As<RE::Actor>();
	}

	// Preserve crosshair miss on Target Actor delivery type.
	if( a_useTargetActor && !crosshairTarget )
	{
		PickDataRayInputCopy( *a_pickData, crosshairPickData, range );
		return crosshairTarget;
	}

	// Aim the origin ray to the crosshair when crosshair hit something or the spell is Target Location delivery type.
	auto handDirection = hitCrosshair - HavokToWorld<RE::NiPoint3>( a_origin );
	handDirection.Unitize();

	a_pickData->rayInput.from = a_origin;
	a_pickData->ray = handDirection * range;

	if( Settings::bShowLastProjectilePath )
	{
		TrueHUDArrowTrail::BeginRay(
			HavokToWorld<RE::NiPoint3>( a_pickData->rayInput.from ),
			HavokToWorld<RE::NiPoint3>( a_pickData->rayInput.from + a_pickData->ray ) );
	}

	return a_useTargetActor ? crosshairTarget : nullptr;
}

void Raycast::SetLastImpactLocation( const RE::NiPoint3& a_location, uint32_t a_expireMS )
{
	lastRayCoord = a_location;
	rayImpactExpire = GetCurrentGameRuntimeMS() + a_expireMS;
}

bool Raycast::EvaluateRayTargetActor( RE::bhkPickData* a_pickData, RE::Actor* a_target )
{
	if( !a_pickData )
		return false;

	bool isTargetValid = false;
	if( a_pickData && a_pickData->rayOutput.rootCollidable )
	{
		auto hitRef = RE::TESHavokUtilities::FindCollidableRef( *a_pickData->rayOutput.rootCollidable );
		if( hitRef )
		{
			auto hitActor = hitRef->As<RE::Actor>();
			isTargetValid = hitActor && hitActor == a_target;
		}
	}

	return isTargetValid;
}

void Raycast::Update()
{
	if( Settings::IsCrosshairImpactDebugEnabled() )
	{
		if( rayImpactExpire > GetCurrentGameRuntimeMS() && lastRayCoord != RE::NiPoint3::Zero() )
			Crosshair::SetToWorldPosition( Raycast::lastRayCoord );
		else
			Crosshair::ResetPosition();
	}
}


