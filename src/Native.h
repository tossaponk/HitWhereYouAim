#pragma once

namespace Native
{
	inline void ProjectBoundToScreen( const RE::NiCamera* a_camera, const RE::NiBound& a_bound, RE::NiPoint3& out_projectedMin, RE::NiPoint3& out_projectedMax, float a_nearThreshold )
	{
		using func_t = decltype(&ProjectBoundToScreen);
		static REL::Relocation<func_t> func{ RELOCATION_ID(69265, 70632) };
		func( a_camera, a_bound, out_projectedMin, out_projectedMax, a_nearThreshold );
	}

	inline void UpdateAutoAimTarget( RE::PlayerCharacter* a_player, float a_maxDistance, bool a_alternateMode )
	{
		using func_t = decltype(&UpdateAutoAimTarget);
		static REL::Relocation<func_t> func{ RELOCATION_ID(40243, 41246) };
		func( a_player, a_maxDistance, a_alternateMode );
	}

	inline int8_t RequestDetectionResult( RE::Actor* a_observer, RE::Actor* a_target, RE::DETECTION_PRIORITY a_priority )
	{
		using func_t = decltype(&RequestDetectionResult);
		static REL::Relocation<func_t> func{ RELOCATION_ID(36745, 37761) };
		return func( a_observer, a_target, a_priority );
	}

	// World gravity acceleration used by Skyrim projectile ballistic prediction.
	// Native CombatProjectileAimController multiplies Projectile::GetGravity()
	// by this value before solving the trajectory.
	[[maybe_unused]]
	inline float GetWorldGravity()
	{
		static REL::Relocation<const float*> worldGravity{ RELOCATION_ID(518746, 405286) };
		return *worldGravity;
	}

	[[maybe_unused]]
	inline RE::NiPoint3* CalculatePredictedProjectileAimPoint( RE::NiPoint3* a_outAimPoint, RE::NiPoint3* a_origin, RE::NiPoint3* a_targetPosition, RE::NiPoint3* a_targetVelocity,
		float a_projectileSpeed, float a_gravity, bool a_highArc )
	{
		// Check reachablility because the hooked function doesn't do it
		if( a_projectileSpeed <= 0.0F || a_gravity <= 0.0F )
		{
			return nullptr;
		}

		float x_delta = a_targetPosition->x - a_origin->x;
		float y_delta = a_targetPosition->y - a_origin->y;
		float z_delta = a_targetPosition->z - a_origin->z;

		float horizontalDistanceSquared = x_delta * x_delta + y_delta * y_delta;

		// Hooked function doesn't like the distance larger than 8192 game unit
		if( horizontalDistanceSquared + z_delta * z_delta >= 8192.0f * 8192.0f )
			return nullptr;

		float speedSquared = a_projectileSpeed * a_projectileSpeed;
		float discriminant =
			speedSquared * speedSquared -
			a_gravity *
			(
				a_gravity * horizontalDistanceSquared +
				2.0F * z_delta * speedSquared
				);

		// Return if cannot reach the target
		if( discriminant <= 0 )
			return nullptr;

		using func_t = decltype(&CalculatePredictedProjectileAimPoint);
		static REL::Relocation<func_t> func{ RELOCATION_ID(46025, 47289) };
		return func( a_outAimPoint, a_origin, a_targetPosition, a_targetVelocity, a_projectileSpeed, a_gravity, a_highArc );
	}

	inline void SetCrosshairValidity( bool a_valid )
	{
		using func_t = decltype(&SetCrosshairValidity);
		static REL::Relocation<func_t> func{ RELOCATION_ID( 50738, 51633 ) };
		func( a_valid );
	}

	inline RE::hkpWorld* GetHavokWorldFromCell( RE::TESObjectCELL* a_cell )
	{
		if( !a_cell )
			return nullptr;

		using func_t = decltype(&GetHavokWorldFromCell);
		static REL::Relocation<func_t> func{ RELOCATION_ID( 18536, 18995 ) };
		return func( a_cell );
	}
}
