#include "Ballistics.h"
#include "BallisticSolver.h"
#include "Settings.h"
#include "Utils.h"
#include "Raycast.h"
#include "TargetSelection.h"
#include "Native.h"

static float AngleDifference( float a_corrected, float a_original )
{
	return std::remainder( a_corrected - a_original, 2.0F * std::numbers::pi_v<float> );
}

bool Ballistics::Intercept( const RE::NiPoint3& a_origin, const RE::NiPoint3& a_target, const RE::NiPoint3* a_targetVelocity, float a_projectileSpeed, float a_projectileGravity, RE::NiPoint3* out_intercept, float* out_angleX, float* out_angleZ, float* out_flightTime )
{
    RE::NiPoint3 targetVelocity{ a_targetVelocity ? *a_targetVelocity : RE::NiPoint3::Zero() };

    // Convert to Y-up solver coord (Skyrim uses Z-up)
    BallisticSolver::Vector3 solverOrigin{ a_origin.x,			a_origin.z,			a_origin.y };
    BallisticSolver::Vector3 solverTarget{ a_target.x,			a_target.z,			a_target.y };
    BallisticSolver::Vector3 solverTargetVelocity{ targetVelocity.x,		targetVelocity.z,	targetVelocity.y };

    BallisticSolver::Result result;
    if( BallisticSolver::Solve(
        solverOrigin,
        solverTarget,
        solverTargetVelocity,
        a_projectileSpeed,
        a_projectileGravity,
        result, false ) )
    {
        // Convert back to Skyrim coord
        if( out_intercept )
        {
            out_intercept->x = (float)result.interceptPosition.x;
            out_intercept->y = (float)result.interceptPosition.z;
            out_intercept->z = (float)result.interceptPosition.y;
        }

        if( out_angleX ) *out_angleX = (float)-result.pitch; // Skyrim negative pitch is upward
        if( out_angleZ ) *out_angleZ = (float)result.yaw;
		if( out_flightTime ) *out_flightTime = (float)result.flightTime;

        return true;
    }

    return false;
}

RE::NiPoint3 Ballistics::ExtendVectorFromOrigin( const RE::NiPoint3& a_origin, const RE::NiPoint3& a_target, float a_extension )
{
	RE::NiPoint3 delta = a_target - a_origin;
	float length = delta.Length();

	if( length == 0 )
		return a_target;

	RE::NiPoint3 direction = delta / length;

	return a_target + direction * a_extension;
}

RE::NiPoint3 Ballistics::ClampVectorFromOrigin( const RE::NiPoint3& a_origin, const RE::NiPoint3& a_target, float a_maxLength )
{
	RE::NiPoint3 delta = a_target - a_origin;
	float length = delta.Length();

	if( length == 0 || length < a_maxLength )
		return a_target;

	RE::NiPoint3 direction = delta / length;

	return a_origin + direction * a_maxLength;
}

float Ballistics::CalculateTimeToTarget( const RE::NiPoint3& a_origin, const RE::NiPoint3& a_target, float a_speed )
{
	if( a_speed > 0 )
	{
		auto targetDelta = a_target - a_origin;
		auto targetDist = targetDelta.Length();
		return targetDist / a_speed;
	}

	return INFINITY;
}

bool Ballistics::CalculateAnglesToTarget( const RE::NiPoint3& a_origin, const RE::NiPoint3& a_target, float& a_angleX, float& a_angleZ )
{
	RE::NiPoint3 delta = a_target - a_origin;
	float horizontalSquared = delta.x * delta.x + delta.y * delta.y;
	float lengthSquared = horizontalSquared + delta.z * delta.z;

	if( lengthSquared <= 0.000001f )
		return false;

	float horizontal = sqrt( horizontalSquared );
	a_angleX = -atan2( delta.z, horizontal );

	if( horizontalSquared > 0.000001f )
		a_angleZ = atan2( delta.x, delta.y );

	return true;
}

bool Ballistics::SetProjectileAngle( RE::Projectile* a_projectile, float a_angleX, float a_angleZ )
{
	if( !a_projectile )
		return false;

	auto& runtimeData = a_projectile->GetProjectileRuntimeData();
	float speed = runtimeData.linearVelocity.Length();

	if( speed <= 0.000001f )
		return false;

	float sinX = sin( a_angleX );
	float cosX = cos( a_angleX );
	float sinZ = sin( a_angleZ );
	float cosZ = cos( a_angleZ );

	const RE::NiPoint3 direction{
		sinZ * cosX,
		cosZ * cosX,
		-sinX
	};

	a_projectile->data.angle.x = a_angleX;
	a_projectile->data.angle.z = a_angleZ;

	runtimeData.linearVelocity = direction * speed;

	if( auto* projectile3D = a_projectile->Get3D() )
	{
		auto& rotation = projectile3D->local.rotate;

		rotation.entry[ 0 ][ 0 ] = cosZ;
		rotation.entry[ 0 ][ 1 ] = direction.x;
		rotation.entry[ 0 ][ 2 ] = sinZ * sinX;

		rotation.entry[ 1 ][ 0 ] = -sinZ;
		rotation.entry[ 1 ][ 1 ] = direction.y;
		rotation.entry[ 1 ][ 2 ] = cosZ * sinX;

		rotation.entry[ 2 ][ 0 ] = 0.0F;
		rotation.entry[ 2 ][ 1 ] = direction.z;
		rotation.entry[ 2 ][ 2 ] = cosX;
	}

	return true;
}

float Ballistics::CalculateImpactDepth( const RE::NiPoint3& a_fireOrigin, const RE::NiPoint3& a_aimPosition, const RE::NiPoint3& a_impactPosition )
{
	RE::NiPoint3 direction = a_impactPosition - a_aimPosition;
	float distanceSquared = direction.SqrLength();

	if( distanceSquared <= 1.0e-6f )
		return 0.0f;

	direction /= sqrt( distanceSquared );

	return ( a_impactPosition - a_fireOrigin ).Dot( direction );
}

bool Ballistics::CalculateFallbackBallisticAngles( const RE::NiPoint3& a_origin, const RE::NiPoint3& a_target, float a_projectileSpeed, float a_gravity, float& a_pitch, float& a_yaw )
{
	RE::NiPoint3 delta = a_target - a_origin;

	float horizontalDistance = sqrt( delta.x * delta.x + delta.y * delta.y );

	if(	!isfinite( horizontalDistance ) ||
		!isfinite( delta.z ) ||
		!isfinite( a_projectileSpeed ) ||
		!isfinite( a_gravity ) ||
		a_projectileSpeed <= 0.0f )
	{
		return false;
	}

	a_yaw = atan2( delta.x, delta.y );

	if( a_gravity <= 0.0001f )
	{
		a_pitch = atan2( delta.z, horizontalDistance );
		return isfinite( a_pitch ) && isfinite( a_yaw );
	}

	float speedSquared = a_projectileSpeed * a_projectileSpeed;
	float elevationTerm = speedSquared + 2.0f * a_gravity * delta.z;

	if( elevationTerm <= 0.0f )
	{
		// The target is so far below the origin that the maximum-range
		// equation has no finite tangent solution. Aim directly instead.
		a_pitch = atan2( delta.z, horizontalDistance );
	}
	else
	{
		a_pitch = atan2( a_projectileSpeed, sqrt( elevationTerm ) );
	}

	return isfinite( a_pitch ) && isfinite( a_yaw );
}

RE::NiPoint3 Ballistics::GetPlayerAimPosition()
{
	auto player = RE::PlayerCharacter::GetSingleton();

	RE::NiPoint3 aimPos = player->GetPosition();
	auto aimHeight = player->IsSneaking() ? Settings::fSneakingAimHeight : Settings::fStandingAimHeight;
	aimPos.z += aimHeight * meterToGameUnit * player->GetScale();

	return aimPos;
}

std::pair<RE::TESObjectREFR*,RE::TESObjectREFR*> Ballistics::ResolveCrosshairAim( RE::NiPoint3& a_origin, float a_projectileSpeed, float a_projectileGravity, RE::NiPoint3* out_aimPoint, RE::NiPoint3* out_TargetLoc, float* out_pitch, float* out_yaw )
{
	RE::TESObjectREFR* crosshairTarget = nullptr;
	RE::TESObjectREFR* predictedTarget = nullptr;

	if( a_projectileSpeed <= 0 )
		return std::make_pair( crosshairTarget, predictedTarget );

	auto originalPitch = out_pitch ? *out_pitch : FLT_MAX;
	auto originalYaw = out_yaw ? *out_yaw : FLT_MAX;
	auto shooter = RE::PlayerCharacter::GetSingleton();
	auto camera = RE::Main::WorldRootCamera();
	RE::CFilter filter; shooter->GetCollisionFilterInfo( filter );

	RE::NiPoint3 aimLocation;
	RE::bhkPickData pickData;
	RE::TESObjectREFRPtr cachedTarget;
	if( !Raycast::GetReleaseTargetCapture( &aimLocation, &pickData, &cachedTarget ) )
	{
		crosshairTarget = Raycast::GetCrosshairTarget( filter.GetSystemGroup(), RE::COL_LAYER::kLOS, &aimLocation, &pickData);
	}
	else
	{
		crosshairTarget = cachedTarget.get();
	}

	predictedTarget = crosshairTarget;
	bool mustUseCrosshair = crosshairTarget != nullptr;
	if( mustUseCrosshair )
	{
		if( auto actor = crosshairTarget->As<RE::Actor>() )
		{
			if( actor->IsDead() || actor->IsDisabled() || actor->IsDeleted() )
			{
				mustUseCrosshair = false;
			}
		}
		else
		{
			mustUseCrosshair = false;
		}
	}

	RE::NiPoint3 interceptLocation = aimLocation;

	auto aimPos				= GetPlayerAimPosition();
	auto aimZeroDistance	= Settings::fMaximumTargetDistance * meterToGameUnit;
	auto impactDepth		= Ballistics::CalculateImpactDepth( a_origin, aimPos, aimLocation );
	auto gravity			= a_projectileGravity;
	PrintDebugInfo( "Crosshair impact depth = {}", impactDepth );

	// Cancel launch angle modification if the ray impact is behind the projectile origin
	bool correctionValid = false;
	if( impactDepth > 0 )
	{
		if( Settings::bTargetDepthPrediction && (!crosshairTarget || !mustUseCrosshair) )
		{
			// Clamp depth to potential target if there is one
			RE::NiPoint3 rayDelta = camera->world.translate - aimLocation;
			auto [potentialTarget,predictPos] = TargetSelection::GetPotentialPlayerTarget(
				rayDelta.Length(),
				a_projectileSpeed,
				a_projectileGravity,
				&a_origin,
				Settings::fPredictionWindowWidth,
				Settings::fPredictionWindowHeight,
				nullptr, Settings::IsFixedAngleEnabled() );

			if( potentialTarget )
			{
				predictedTarget = potentialTarget;
				auto predictedDist = (predictPos - camera->world.translate).Length();
				aimZeroDistance = predictedDist;

				if( Settings::bShowTargetPredictionInfo )
					PrintDebugInfo( "Predicted aim target: {}, distance: {}", potentialTarget->GetDisplayFullName(), aimZeroDistance );
			}
		}

		aimLocation = Ballistics::ClampVectorFromOrigin( camera->world.translate, aimLocation, aimZeroDistance );

		// Add ray penetration
		aimLocation = Ballistics::ExtendVectorFromOrigin( camera->world.translate, aimLocation, Settings::fRayPenetrationDepth * meterToGameUnit );

		interceptLocation = aimLocation;

		RE::NiPoint3 targetVelocity{ 0, 0, 0 };

		// Get target velocity to calculate intercept
		if( Settings::IsTargetLeadingEnabled() && predictedTarget && crosshairTarget == predictedTarget )
		{
			predictedTarget->GetLinearVelocity( targetVelocity );
			auto targetName =  predictedTarget->GetDisplayFullName();
			PrintDebugInfo( "Project aim correction ray target: {}", targetName ? targetName : "" );
		}

		Raycast::SetLastImpactLocation( aimLocation, 5000 );
		
		float interceptPitch = 0, interceptYaw = 0;
		float flightTime = 0;
		// Use ballistic solver to solve the aim pitch
		if( Settings::IsAutomaticPitchEnabled() )
		{
			RE::NiPoint3 predicted;
			if( Native::CalculatePredictedProjectileAimPoint(
				&predicted,
				&a_origin,
				&aimLocation,
				&targetVelocity,
				a_projectileSpeed,
				gravity,
				false ) )
			{
				correctionValid = true;
				PrintDebugInfo( "Aim assist mode: Skyrim" );
				aimLocation = predicted;
				flightTime = (aimLocation - a_origin).Length() / a_projectileSpeed;
				interceptLocation = aimLocation;
				interceptLocation.z -= 0.5f * gravity * flightTime * flightTime;
			}
			else // Use custom solver if the Skyrim solver cannot be used
			{
				PrintDebugInfo( "Aim assist mode: Ballistic Solver" );
				if( Intercept(
					a_origin,
					aimLocation,
					&targetVelocity,
					a_projectileSpeed,
					gravity,
					&interceptLocation,
					&interceptPitch,
					&interceptYaw,
					&flightTime ) )
				{
					correctionValid = true;
					RE::NiPoint3 aimPoint = interceptLocation;
					aimPoint.z += 0.5f * gravity * flightTime * flightTime;
					aimLocation = aimPoint;
				}
				else if( originalPitch == FLT_MAX || originalYaw == FLT_MAX )
				{
					PrintDebugInfo( "Ballistic Solver failed. Projectile Speed: {}", a_projectileSpeed );
					CalculateFallbackBallisticAngles(
						a_origin,
						aimLocation,
						a_projectileSpeed,
						gravity,
						originalPitch,
						originalYaw );
				}
			}
		}
		else
		{
			// Shoot at the crosshair directly
			correctionValid = true;
		}
	}

	float pitch = originalPitch != FLT_MAX ? originalPitch : 0.0f;
	float yaw = originalYaw != FLT_MAX ? originalYaw : 0.0f;
	bool isCorrectionFailed = false;
	if( !correctionValid ) // Fallback for impact behind fire origin or solver error
	{
		// Adjust the projectile to current heading direction
		float heading = 0.0f;
		if( shooter->GetGraphVariableFloat( "HWYA_AimHeadingCurrent", heading ) )
		{
			yaw = std::remainder( yaw - heading * degToRad, twoPi	);
		}

		// Use the pitch directly from the AI process (This directly drives the aim pitch angle)
		auto& runtimeData = shooter->GetActorRuntimeData();
		if( runtimeData.currentProcess && runtimeData.currentProcess->middleHigh )
		{
			float shooterPitch = runtimeData.currentProcess->middleHigh->rotation.x;
			pitch = -shooterPitch;
		}

		isCorrectionFailed = true;
	}
	else
	{
		CalculateAnglesToTarget( a_origin, aimLocation, pitch, yaw );

		// Ignore angle correction greater than limit when using target leading mode
		if( Settings::IsTargetLeadingEnabled() && originalPitch != FLT_MAX && originalYaw != FLT_MAX )
		{
			float angleDeltaLimit = Settings::fMaxLeadingAngle * degToRad;
			float pitchDiff = AngleDifference( pitch, originalPitch );
			float yawDiff = AngleDifference( yaw, originalYaw );
			if( pitchDiff >  angleDeltaLimit || yawDiff >  angleDeltaLimit ||
				pitchDiff < -angleDeltaLimit || yawDiff < -angleDeltaLimit )
			{
				pitch = originalPitch;
				yaw = originalYaw;

				isCorrectionFailed = true;
			}
		}
	}

	// Calculate aim point to the original direction
	if( isCorrectionFailed )
	{
		RE::NiPoint3 direction{
			sin( yaw ) * cos( pitch ),
			cos( yaw ) * cos( pitch ),
			sin( pitch )
		};

		float aimDistance = impactDepth > 0 ? Settings::fMaximumTargetDistance * meterToGameUnit : 1.0f;
		aimLocation = a_origin + direction * aimDistance;
		interceptLocation = aimLocation;
	}

	// Pitch offset for projectile with gravity in fixed angle mode
	if( Settings::IsFixedAngleEnabled() && a_projectileGravity > 0.0f && !isCorrectionFailed )
	{
		pitch -= Settings::fPitchOffsetDegree * degToRad;
	}

	// Pitch calibration for bow and crossbow
	auto equippedObject = shooter->GetEquippedObject( false );
	if( equippedObject && equippedObject->IsWeapon() )
	{
		auto weapon = equippedObject->As<RE::TESObjectWEAP>();
		if(  weapon &&
			(weapon->IsBow() || weapon->IsCrossbow()) )
		{
			pitch -= Settings::fArcheryPitchCalibration * degToRad;
		}
	}

	float deltaX = aimLocation.x - a_origin.x;
	float deltaY = aimLocation.y - a_origin.y;
	float horizontalDistance = sqrt( deltaX * deltaX + deltaY * deltaY );
	if( horizontalDistance > 0.000001f )
	{
		aimLocation.z = a_origin.z + tan( pitch ) * horizontalDistance;
	}

	if( out_pitch )		*out_pitch = pitch;
	if( out_yaw )		*out_yaw = yaw;
	if( out_aimPoint )	*out_aimPoint = aimLocation;
	if( out_TargetLoc )	*out_TargetLoc = interceptLocation;

	return std::make_pair( crosshairTarget, predictedTarget );
}
