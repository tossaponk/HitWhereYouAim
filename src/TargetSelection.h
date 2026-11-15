#pragma once

struct ProjectedBounds
{
	RE::NiPoint2 min{ FLT_MAX, FLT_MAX };
	RE::NiPoint2 max{ FLT_MAX, FLT_MAX };
	bool valid = false;
};

struct ProjectedSweep
{
	std::vector<RE::NiPoint2> points;
	ProjectedBounds bounds;
	bool valid = false;
};

struct TargetCandidateInfo
{
	RE::ObjectRefHandle handle;
	float verticalRadius = 0.0f;
	float horizontalRadius = 0.0f;
	float score = FLT_MAX;
	bool isHostile = false;
	bool validProjection = false;
	bool insideSelectionWindow = false;
	bool passedFriendlySelection = false;
	bool hasLineOfSight = false;
	bool selected = false;
};

struct CandidateSelectionInfo
{
	std::vector<TargetCandidateInfo> candidates;
	float xLimit = 0.25f;
	float yLimitUp = 0.25f;
	float yLimitDown = 0.25f;

	void Reset( float a_xLimit, float a_yLimitUp, float a_yLimitDown )
	{
		candidates.clear();
		xLimit = a_xLimit;
		yLimitUp = a_yLimitUp;
		yLimitDown = a_yLimitDown;
	}
};

namespace TargetSelection
{
	bool IsScreenCenterInsideProjectedSweep( const ProjectedSweep& a_sweep );

	ProjectedBounds ProjectReferenceBoundsToScreen(
		const RE::NiCamera*			a_camera,
		const RE::TESObjectREFR*	a_ref,
		float						a_predictionTime,
		float						a_nearThreshold );

	ProjectedSweep GetProjectedSweep(
		const ProjectedBounds&		a_currentBounds,
		const ProjectedBounds&		a_predictedBounds );

	ProjectedBounds ExtendProjectedBoundsUpwardForGravity(
		const RE::NiCamera*			a_camera,
		const ProjectedBounds&		a_bounds,
		const RE::NiPoint3&			a_fireOrigin,
		const RE::NiPoint3&			a_targetPosition,
		float						a_projectileSpeed,
		float						a_projectileGravity );

	ProjectedBounds GetHorizontallyInsetBounds(
		const ProjectedBounds&		a_bounds,
		float						a_inset );

	std::pair<RE::TESObjectREFR*,RE::NiPoint3> GetPotentialPlayerTarget(
		float						a_distance,
		float						a_projectileSpeed = 0.0f,
		float						a_projectileGravity = 0.0f,
		const RE::NiPoint3*			a_fireOrigin = nullptr,
		float						a_xLimit = 0.25f,
		float						a_yLimit = 0.25f,
		CandidateSelectionInfo*		out_debugInfo = nullptr,
		bool						a_fixedPitchMode = false );

}
