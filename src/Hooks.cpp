#include "Hooks.h"
#include "Native.h"
#include "Ballistics.h"
#include "ProjectilePrediction.h"
#include "TargetSelection.h"
#include "TrueDirectionalMovementAPI.h"
#include "ActorState.h"

extern TDM_API::IVTDM1* g_TDMAPI;
bool Hooks::Install()
{
	MH_Initialize();

	PlayerProjectileCalculateLaunchAngleHook::Install();
	ConcentrationSpellAngleHook::Install();
	PlayerSetAimPitchHook::Install();
	PlayerUpdateHook::Install();
	SpellRaycastTargetHook::Install();
	MagicCasterGetCrosshairActorHook::Install();
	SpellTargetFinderHook::Install();
	PredictorGetKillOnCollisionHook::Install();

	//Test::SetGraphVarFloatHook::Install();
	//Test::VATSRangedEvaluationHook::Install();
	//Test::HitDataCopyHook::Install();

	return true;
}

static float pitchVelocity = 0.0f;

void Hooks::PlayerProjectileCalculateLaunchAngleHook::CalculatePlayerProjectileLaunchAngle(
	[[maybe_unused]] RE::PlayerCharacter* a_player,
	RE::Projectile* a_projectile,
	RE::NiAVObject* a_launchNode,
	float* out_yaw,
	float* out_pitch,
	RE::NiPoint3* a_origin,
	[[maybe_unused]] float a_unk7,
	[[maybe_unused]] float a_unk8 )
{
	if( a_projectile == nullptr )
		return;

	if( a_launchNode == nullptr && a_origin == nullptr )
		return;

	auto playerCamera = RE::PlayerCamera::GetSingleton();
	auto cameraState = playerCamera && playerCamera->currentState ? playerCamera->currentState.get() : nullptr;

	if( !cameraState )
		return;

	auto thirdPersonState	= skyrim_cast<RE::ThirdPersonState*>( cameraState );
	bool isFreeRotation		= thirdPersonState && thirdPersonState->GetFreeRotationMode();
	if( Settings::bEnabled &&
		(
			// Disable if free rotation is enabled for FPS and TPS
			(
				( !isFreeRotation || thirdPersonState->freeRotation.x < 0.1f )
				&&
				(
					cameraState->id == RE::CameraState::kFirstPerson ||
					cameraState->id == RE::CameraState::kThirdPerson
					)
				) ||
			// Allow free rotation for mounted combat
			cameraState->id == RE::CameraState::kMount
			)
		)
	{
		auto projectileSpeed = a_projectile->GetSpeed();
		auto projectileGravity = a_projectile->GetGravity() * Native::GetWorldGravity();
		auto target = Ballistics::ResolveCrosshairAim(
			a_origin ? *a_origin : a_launchNode->world.translate,
			projectileSpeed,
			projectileGravity,
			nullptr,
			nullptr,
			out_pitch,
			out_yaw );

		//REX::DEBUG( "Live speed components: base={}, powerMult={}, weaponMult={}, projectileMult={}",
		//	a_projectile->GetProjectileBase()->data.speed,
		//	a_projectile->GetPowerSpeedMult(),
		//	a_projectile->GetWeaponSpeedMult(),
		//	a_projectile->GetProjectileRuntimeData().speedMult );

		//REX::DEBUG( "Live Projectile[ Speed:{}, Gravity:{} ]", projectileSpeed, projectileGravity );

		if( target )
			PrintDebugInfo( "Projectile Target: {}", target->GetDisplayFullName() );

		// Cache firing offset
		RE::NiTransform playerTransform;
		a_player->GetTransform( playerTransform );
		RE::NiPoint3 localOffset = playerTransform.Invert() * a_projectile->GetPosition();

		ProjectilePrediction::MagicOriginCalibrationManager::GetSingleton()->AddSample(
			a_projectile->GetProjectileRuntimeData().castingSource,
			a_player->IsSneaking(),
			localOffset,
			*out_pitch );

		// Mark release target for delete when used
		Raycast::MarkReleaseTargetConsumed();
	}
}

float Hooks::ConcentrationSpellAngleHook::SetAimPitch( RE::Projectile* a_projectile, float a_angle, func_t* func )
{
	bool isTDMLockedOn = g_TDMAPI && g_TDMAPI->GetTargetLockState();
	auto shooter = a_projectile->GetProjectileRuntimeData().shooter;
	if( Settings::bEnabled && !isTDMLockedOn && shooter.native_handle() == 0x100000 && playerPitch != FLT_MAX )
	{
		auto cameraState = GetCurrentPlayerCameraState();
		if( cameraState == RE::CameraState::kFirstPerson ||
			cameraState == RE::CameraState::kThirdPerson ||
			cameraState == RE::CameraState::kMount )
		{
			return func( a_projectile, playerPitch );
		}
	}

	return func( a_projectile, a_angle );
}

float Hooks::ConcentrationSpellAngleHook::SetAimYaw( RE::Projectile* a_projectile, float a_angle, func_t* func )
{
	bool isTDMLockedOn = g_TDMAPI && g_TDMAPI->GetTargetLockState();
	auto& runtimeData = a_projectile->GetProjectileRuntimeData();
	auto shooter = runtimeData.shooter;
	if( Settings::bEnabled && !isTDMLockedOn && shooter.native_handle() == 0x100000 )
	{
		auto cameraState = GetCurrentPlayerCameraState();
		if( cameraState == RE::CameraState::kFirstPerson ||
			cameraState == RE::CameraState::kThirdPerson ||
			cameraState == RE::CameraState::kMount )
		{
			auto actor = shooter.get()->As<RE::Actor>();
			RE::NiPoint3 hitLocation;
			RE::bhkPickData pickData;
			Raycast::GetCrosshairTarget(
				a_projectile->GetCollisionGroup(),
				RE::COL_LAYER::kSpell,
				&hitLocation,
				&pickData
			);

			auto caster			= actor->GetMagicCaster( runtimeData.castingSource );
			auto magicNode		= caster ? caster->GetMagicNode() : nullptr;
			auto origin			= magicNode ? magicNode->world.translate : shooter.get()->GetPosition();
			auto rayEnd			= ((RE::NiPoint3&)(pickData.rayInput.to.quad.m128_f32) / havokScaling);
			auto impactDepth	= Ballistics::CalculateImpactDepth( origin, Ballistics::GetPlayerAimPosition(), rayEnd );

			if( impactDepth > 0 )
			{
				Ballistics::CalculateAnglesToTarget( origin, hitLocation, playerPitch, playerYaw );
				return func( a_projectile, playerYaw );
			}
			else
			{
				playerPitch = playerYaw = FLT_MAX;
			}
		}
	}

	return func( a_projectile, a_angle );
}

static float currentPitch = FLT_MAX;
extern RE::CameraState g_lastCameraState;
float Hooks::PlayerSetAimPitchHook::PlayerGetAimPitch( RE::AIProcess* a_process, const float a_pitch )
{
	float result = a_pitch;

	RE::PlayerCharacter* player = RE::PlayerCharacter::GetSingleton();

	auto currentCameraState = GetCurrentPlayerCameraState();
	bool isFirstPerson = currentCameraState == RE::CameraState::kFirstPerson;
	bool isThirdPerson = currentCameraState == RE::CameraState::kThirdPerson;

	if( Settings::bEnabled &&
		( isFirstPerson || isThirdPerson ) &&
		a_process == player->GetActorRuntimeData().currentProcess )
	{
		RangedAttackType rangedType;
		RE::TESObjectWEAP* weapon;
		RE::MagicItem* spell;
		RE::MagicSystem::CastingSource source;
		RE::BGSProjectile* projectileBase;

		bool hasRangedAttack = GetRangedAttackData( player, &rangedType, &weapon, &spell, &projectileBase, &source );
		bool shouldCorrectAimPitch =
			isThirdPerson &&
			( hasRangedAttack || Settings::bAlwaysCorrectAimPitch );

		if( hasRangedAttack || shouldCorrectAimPitch )
		{
			if( isThirdPerson && currentPitch == FLT_MAX )
				currentPitch = a_pitch;

			float lookPitch = isThirdPerson ? currentPitch : FLT_MAX;
			float lookYaw = FLT_MAX;

			RE::NiPoint3 lookTarget;
			RE::NiPoint3 targetLocation;

			auto aimPos = Ballistics::GetPlayerAimPosition();
			if( !projectileBase )
			{
				currentPitch = FLT_MAX;
				pitchVelocity = 0.0f;
				g_lastCameraState = currentCameraState;
				return result;
			}

			float projectileSpeed = projectileBase->data.speed;
			float projectileGravity = projectileBase->data.gravity * Native::GetWorldGravity();

			bool isSpell = spell != nullptr;

			RE::NiPoint3 origin = player->GetPosition();
			RE::NiNode* launchNode = player->GetFireNode();
			if( !isSpell )
			{
				origin = launchNode ? launchNode->world.translate : aimPos;
			}
			else
			{
				auto magicCaster = player->GetMagicCaster( source );

				if( magicCaster )
					launchNode = magicCaster->GetMagicNode();

				origin = launchNode ? launchNode->world.translate : aimPos;
			}

			[[maybe_unused]]
			auto target =
				Ballistics::ResolveCrosshairAim(
				aimPos,
				projectileSpeed,
				Settings::IsAutomaticPitchEnabled() ? projectileGravity : 0,
				&lookTarget,
				&targetLocation,
				&lookPitch,
				&lookYaw );

			// The calibrated origin is based on third-person release animation.
			// First-person reachability uses only the current live launch node.
			if( isThirdPerson && isSpell )
			{
				if( spell && spell->GetCastingType() != RE::MagicSystem::CastingType::kConcentration )
				{
					RE::NiTransform playerTransform;
					player->GetTransform( playerTransform );
					if( !ProjectilePrediction::MagicOriginCalibrationManager::GetSingleton()->GetOrigin(
						source,
						player->IsSneaking(),
						playerTransform,
						lookPitch,
						origin ) )
					{
						origin = launchNode ? launchNode->world.translate : aimPos;
					}
				}
			}

			//REX::DEBUG( "Pitch: {}, Origin: {} {} {}", lookPitch, origin.x, origin.y, origin.z );
			bool enableAimValidation =
				( isFirstPerson && Settings::bEnableFPSAimValidation ) ||
				( isThirdPerson && Settings::bEnableTPSAimValidation );

			if( enableAimValidation )
			{
				RE::Projectile::LaunchData launchData{};
				launchData.origin = origin;
				launchData.useOrigin = true;
				launchData.noDamageOutsideCombat = true;
				launchData.projectileBase = projectileBase;
				launchData.shooter = player;
				if( isSpell )
				{
					launchData.spell = spell;
					launchData.castingSource = source;
					launchData.power = 1.0f;
				}
				else
				{
					launchData.weaponSource = weapon;
					launchData.ammoSource = player->GetCurrentAmmo();
					launchData.power = weapon ? CalculateBowPower( GetCurrentBowDrawTimer( player ), launchData.weaponSource->GetSpeed() ) : 1.0f;
				}

				RE::CFilter filter;
				player->GetCollisionFilterInfo( filter );
				filter.SetCollisionLayer( isSpell ? RE::COL_LAYER::kSpell : RE::COL_LAYER::kProjectile );

				bool isAiming = false;
				player->GetGraphVariableBool( "bAimActive", isAiming );

				isAiming |= IsCastingRangedSpell( *player, rangedType == RangedAttackType::kLeftHand );

				auto effect = spell ? spell->GetAVEffect() : nullptr;
				auto delivery = effect ? effect->data.delivery : RE::MagicSystem::Delivery::kNone;
				bool isAimedSpell = effect && isAiming &&
					( delivery == RE::MagicSystem::Delivery::kAimed ||
					  delivery == RE::MagicSystem::Delivery::kTargetActor );

				bool isReachabilitySupported = projectileBase->data.types.any(
					RE::BGSProjectileData::Type::kArrow,
					RE::BGSProjectileData::Type::kBeam,
					RE::BGSProjectileData::Type::kMissile );

				bool needPrediction =
					isAimedSpell ||
					((BowDrawArray*)&player->GetPlayerRuntimeData().unkBA0)->size() > 0; // Drawing a bow

				if( needPrediction && isReachabilitySupported )
				{
					bool isShotObstructed = false;
					bool isTargetActor = delivery == RE::MagicSystem::Delivery::kTargetActor;
					bool isBeam = projectileBase->IsBeam();
					bool shouldUseRaycast = isTargetActor || isBeam;

					if( !shouldUseRaycast )
					{
						// Prepare the predictor
						ProjectilePrediction::Predictor predictor( launchData, filter );
						RE::Projectile* predictorProjectile = predictor.GetPredictorProjectile();
						if( predictorProjectile )
						{
							RE::NiPoint3 predictorTargetLoc;
							launchData.angleX = FLT_MAX;
							launchData.angleZ = FLT_MAX;

							projectileSpeed = predictorProjectile->GetSpeed();
							projectileGravity = predictorProjectile->GetGravity() * Native::GetWorldGravity();

							Ballistics::ResolveCrosshairAim(
								origin,
								projectileSpeed,
								projectileGravity,
								nullptr,
								&predictorTargetLoc,
								&launchData.angleX,
								&launchData.angleZ );

							auto predictionResult = predictor.Predict( launchData );
							if( predictionResult.type == ProjectilePredictionResult::kImpacted )
							{
								RE::TESObjectREFRPtr impactRef;
								RE::LookupReferenceByHandle( predictionResult.impact.collidee.native_handle(), impactRef );

								if( impactRef && predictionResult.impact.damageRootNode )
								{
									//REX::DEBUG( "Predictor Impact: {}", predictionResult.impact.damageRootNode->name.c_str() );
								}
								else
								{
									//REX::DEBUG( "Predictor Impact: Static" );
								}

								float rayPenetrationDepth = Settings::fRayPenetrationDepth * meterToGameUnit;
								float impactDistance = (aimPos - predictionResult.location).Length();
								float crosshairDistance = (aimPos - predictorTargetLoc).Length();

								float impactTolerance =
									-10.0f -
									rayPenetrationDepth -
									GetProjectileImpactTolerance( projectileBase, launchData.spell );

								//REX::DEBUG( "Impact depth: {}, crosshair depth: {}", impactDistance, crosshairDistance );

								if( impactDistance - crosshairDistance < impactTolerance )
								{
									isShotObstructed = true;
									//REX::DEBUG( "Applying red tint: Impact blocked" );
								}
							}
						}
					}
					else
					{
						RE::bhkPickData pickData{};
						RE::NiPoint3 rayDestination;

						auto rayTarget = Raycast::GetTarget(
							origin,
							targetLocation,
							filter.GetSystemGroup(),
							RE::COL_LAYER::kLOS,
							&rayDestination,
							&pickData );

						if( isTargetActor )
						{
							// For now, any actor reached by the launch-origin ray is considered valid.
							auto targetActor = rayTarget ? rayTarget->As<RE::Actor>() : nullptr;
							isShotObstructed = !Raycast::EvaluateRayTargetActor( &pickData, targetActor );
						}
						else // Beam
						{
							// A beam is obstructed only when the launch-origin ray hits
							// meaningfully before the crosshair-selected location.
							if( pickData.rayOutput.rootCollidable )
							{
								float rayPenetrationDepth = Settings::fRayPenetrationDepth * meterToGameUnit;
								float impactDistance = (rayDestination - origin).Length();
								float crosshairDistance = (targetLocation - origin).Length();

								isShotObstructed = impactDistance - crosshairDistance < -10.0f - rayPenetrationDepth;
							}
						}

						if( Settings::bShowFlightPrediction )
							TrueHUDArrowTrail::BeginRay( origin, rayDestination );
					}

					if( g_lastCameraState == currentCameraState )
						Crosshair::SetValidityState( !isShotObstructed );
				}
			}

			if( shouldCorrectAimPitch )
			{
				// Third-person model firing angle adjustment for yaw
				if( !Settings::bDisableAimHeading )
					SetAimHeadingCorrection( *player, aimPos, lookTarget );

				// Third-person pitch bias
				float pitchBias = Settings::fPitchBias * degToRad;

				// Reduce bias for extremes because they're already correct.
				float halfPi = std::numbers::pi_v<float> * 0.5f;
				float fadeStart = std::clamp( Settings::fBiasFadingStart * degToRad, 0.0f, halfPi );
				float t = std::clamp(( abs( lookPitch ) - fadeStart ) / ( halfPi - fadeStart ), 0.0f, 1.0f);
				float biasFactor = 1.0f - t * t * ( 3.0f - 2.0f * t );

				pitchBias *= biasFactor;
				lookPitch -= pitchBias;
				result = -lookPitch;

				// Don't snap into place. Do it smoothly.
				float deltaTime = RE::BSTimer::GetSingleton()->delta;
				float maxSpeed = Settings::fMaxPitchAdjustmentSpeed * degToRad;
				float acceleration = maxSpeed * 4.0f; // Reach max speed in 0.25 sec
				float delta = result - currentPitch;
				float distance = abs( delta );

				if( distance < 0.00001f )
				{
					currentPitch = result;
					pitchVelocity = 0.0f;
				}
				else
				{
					float direction = delta > 0 ? 1.0f : -1.0f;
					float brakingSpeed = sqrt( 2.0f * acceleration * distance );
					float targetVelocity = direction * std::min( maxSpeed, brakingSpeed );

					pitchVelocity += std::clamp(
						targetVelocity - pitchVelocity,
						-acceleration * deltaTime,
						acceleration * deltaTime );

					float angleStep = pitchVelocity * deltaTime;

					if( abs( angleStep ) >= distance && angleStep * delta > 0 )
					{
						currentPitch = result;
						pitchVelocity = 0.0f;
					}
					else
					{
						currentPitch += angleStep;
					}
				}

				result = currentPitch;
			}
			else
			{
				currentPitch = FLT_MAX;
				pitchVelocity = 0.0f;
			}
		}
		else
		{
			currentPitch = FLT_MAX;
			pitchVelocity = 0.0f;
		}
	}
	else
	{
		currentPitch = FLT_MAX;
		pitchVelocity = 0.0f;
	}

	g_lastCameraState = currentCameraState;

	return result;
}

void Hooks::ProjectileInitHook::OverrideProjectileInit( RE::Projectile* a_this, RE::NiPoint3& a_angle )
{
	auto& shooter = a_this->GetProjectileRuntimeData().shooter;
	if( shooter.native_handle() == 0x100000 ) // Player handle
	{
		// Hand over the projectile correction to TDM if locked-on
		bool isTDMLockedOn = g_TDMAPI && g_TDMAPI->GetTargetLockState();
		if( Settings::bEnabled && !isTDMLockedOn )
		{
			// Replace with HWYA angle
			auto player = RE::PlayerCharacter::GetSingleton();
			if( GetCurrentPlayerCameraState() == RE::CameraState::kMount )
			{
				auto& biped = player->GetCurrentBiped();
				if( biped &&
					player->AsActorState()->GetAttackState() >= RE::ATTACK_STATE_ENUM::kBowReleasing &&
					player->AsActorState()->GetAttackState() <= RE::ATTACK_STATE_ENUM::kBowReleased )
				{
					auto process = player->GetActorRuntimeData().currentProcess;
					auto fireNode = process->GetWeaponNode( biped );

					if( !fireNode )
						return;

					auto firePosition = fireNode->world.translate;
					auto fireDirection = fireNode->world.rotate * RE::NiPoint3( 0, 1, 0 );

					fireDirection.Unitize();

					float horizontalSquared =
						fireDirection.x * fireDirection.x +
						fireDirection.y * fireDirection.y;

					float lengthSquared = horizontalSquared + fireDirection.z * fireDirection.z;

					if( lengthSquared > 1.e-6f && horizontalSquared > 1.e-6f )
					{
						// Only clamp yaw. Leave pitch as is.
						//float angleX = atan2( -fireDirection.z, sqrt( horizontalSquared ) );
						float angleZ = atan2( fireDirection.x, fireDirection.y );

						float maxVisualDifference = Settings::fMountedYawTolerance * degToRad;
						float yawDifference = std::remainder(a_angle.z - angleZ, 2.0f * std::numbers::pi_v<float>);

						//a_angle.x = angleX;
						a_angle.z = angleZ + std::clamp( yawDifference, -maxVisualDifference, maxVisualDifference );
					}
				}
			}

			Ballistics::SetProjectileAngle( a_this, a_angle.x, a_angle.z );
			if( Settings::bInheritVelocity && !Settings::IsTargetLeadingEnabled() )
			{
				RE::NiPoint3 playerVelocity{ 0, 0, 0 };
				player->GetLinearVelocity( playerVelocity );

				auto& projectileRuntimeData = a_this->GetProjectileRuntimeData();
				projectileRuntimeData.linearVelocity.x += playerVelocity.x;
				projectileRuntimeData.linearVelocity.y += playerVelocity.y;
				// Skyrim default behavior leaves z-velocity out.
				// projectileRuntimeData.linearVelocity.z += playerVelocity.z;
			}
		}

		if( !a_this->IsFlameProjectile() && !a_this->IsBeamProjectile() )
			TrueHUDArrowTrail::BeginArrow( a_this );
	}
}

