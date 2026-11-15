#pragma once

namespace Raycast
{
	RE::TESObjectREFR*	GetTarget(
		const RE::NiPoint3&	a_origin,
		const RE::NiPoint3&	a_target,
		uint32_t			a_collisionGroup,
		RE::COL_LAYER		a_collisionLayer,
		RE::NiPoint3*		out_hitLocation = nullptr,
		RE::bhkPickData*	out_pickData = nullptr );

	RE::TESObjectREFR*	GetScreenTarget(
		uint32_t			a_x,
		uint32_t			a_y,
		uint32_t			a_collisionGroup,
		RE::COL_LAYER		a_collisionLayer,
		RE::NiPoint3*		out_hitLocation = nullptr,
		RE::bhkPickData*	out_pickData = nullptr,
		float				a_maxDistance = 10000.0f );

	RE::TESObjectREFR*	GetCrosshairTarget(
		uint32_t			a_collisionGroup,
		RE::COL_LAYER		a_collisionLayer,
		RE::NiPoint3*		out_hitLocation,
		RE::bhkPickData*	out_pickData );

	RE::TESObjectREFR*	GetCrosshairTarget(
		const RE::CFilter&	a_collisionFilter,
		RE::NiPoint3*		out_hitLocation,
		RE::bhkPickData*	out_pickData );

	RE::Actor*			ResolveRayTarget(
		RE::bhkPickData*	a_pickData,
		RE::hkVector4		a_origin,
		bool				a_useTargetActor );

	void				SetLastImpactLocation(
		const RE::NiPoint3& a_location,
		uint32_t			a_expireMS );

	bool				EvaluateRayTargetActor( RE::bhkPickData* a_pickData, RE::Actor* a_target );

	void				Update();
}
