#pragma once

namespace Ballistics
{
	bool Intercept(
		const RE::NiPoint3&		a_origin,
		const RE::NiPoint3&		a_target,
		const RE::NiPoint3*		a_targetVelocity,
		float					a_projectileSpeed,
		float					a_projectileGravity,
		RE::NiPoint3*			out_intercept,
		float*					out_angleX,
		float*					out_angleZ,
		float*					out_flightTime );

	RE::NiPoint3 ExtendVectorFromOrigin(
		const RE::NiPoint3&		a_origin,
		const RE::NiPoint3&		a_target,
		float					a_extension );

	RE::NiPoint3 ClampVectorFromOrigin(
		const RE::NiPoint3&		a_origin,
		const RE::NiPoint3&		a_target,
		float					a_maxLength );

	float CalculateTimeToTarget(
		const RE::NiPoint3&		a_origin,
		const RE::NiPoint3&		a_target,
		float					a_speed );
	bool CalculateAnglesToTarget(
		const RE::NiPoint3&		a_origin,
		const RE::NiPoint3&		a_target,
		float&					a_angleX,
		float&					a_angleZ );

	bool SetProjectileAngle(
		RE::Projectile*			a_projectile,
		float					a_angleX,
		float					a_angleZ );

	float CalculateImpactDepth(
		const RE::NiPoint3&		a_fireOrigin,
		const RE::NiPoint3&		a_aimPosition,
		const RE::NiPoint3&		a_impactPosition );

	RE::NiPoint3 GetPlayerAimPosition();

	RE::TESObjectREFR* ResolveCrosshairAim(
		RE::NiPoint3& a_origin,
		float a_projectileSpeed,
		float a_projectileGravity,
		RE::NiPoint3* out_aimPoint,
		RE::NiPoint3* out_targetLoc,
		float* out_pitch,
		float* out_yaw );
}

