#include "TargetSelection.h"
#include "Utils.h"
#include "Ballistics.h"
#include "Native.h"

constexpr float friendlyGravityAimErrorMultiplier = 1.25f;

float Cross2D( const RE::NiPoint2& a_origin, const RE::NiPoint2& a_point1, const RE::NiPoint2& a_point2 )
{
	return
		( a_point1.x - a_origin.x ) * ( a_point2.y - a_origin.y ) -
		( a_point1.y - a_origin.y ) * ( a_point2.x - a_origin.x );
}

ProjectedBounds TargetSelection::ProjectReferenceBoundsToScreen( const RE::NiCamera* a_camera, const RE::TESObjectREFR* a_ref, float a_predictionTime, float a_nearThreshold )
{
    ProjectedBounds result;

    if( a_camera && a_ref )
    {
        auto ref3D = a_ref->Get3D2();
        if( ref3D )
        {
            RE::NiBound bound = ref3D->worldBound;
            RE::NiPoint3 moveDelta{ 0, 0, 0 };

            // Predict future position before projecting onto the screen
            RE::NiPoint3 velocity;
            a_ref->GetLinearVelocity( velocity );
            moveDelta = velocity * a_predictionTime;

            bound.center.x = a_ref->GetPositionX();
            bound.center.y = a_ref->GetPositionY();
            bound.center += moveDelta;

            float normalizedX;
            float normalizedY;
            float depth;

            if( !RE::NiCamera::WorldPtToScreenPt3(
                a_camera->GetRuntimeData().worldToCam,
                a_camera->GetRuntimeData2().port,
                bound.center,
                normalizedX,
                normalizedY,
                depth,
                1.0e-5f ) )
            {
                return result;
            }

            if( depth <= 0.0f || depth >= 1.0f )
                return result;

            RE::NiPoint3 projectedYMin, projectedYMax;
            Native::ProjectBoundToScreen( a_camera, bound, projectedYMin, projectedYMax, a_nearThreshold );

            bound.radius = a_ref->GetBoundMax().x;
            RE::NiPoint3 projectedXMin, projectedXMax;
            Native::ProjectBoundToScreen( a_camera, bound, projectedXMin, projectedXMax, a_nearThreshold );

            if( projectedXMin.x == -1.0f && projectedXMax.x == 1.0f )
                return result;

            if( projectedYMin.y == -1.0f && projectedYMax.y == 1.0f )
                return result;

            result.min.x = projectedXMin.x;
            result.min.y = projectedYMin.y;

            result.max.x = projectedXMax.x;
            result.max.y = projectedYMax.y;

            result.valid = true;
        }
    }

    return result;
}

ProjectedSweep TargetSelection::GetProjectedSweep( const ProjectedBounds& a_currentBounds, const ProjectedBounds& a_predictedBounds )
{
	ProjectedSweep result;

	if( !a_currentBounds.valid || !a_predictedBounds.valid )
		return result;

	std::vector<RE::NiPoint2> points{
		{ a_currentBounds.min.x, a_currentBounds.min.y },
		{ a_currentBounds.min.x, a_currentBounds.max.y },
		{ a_currentBounds.max.x, a_currentBounds.min.y },
		{ a_currentBounds.max.x, a_currentBounds.max.y },
		{ a_predictedBounds.min.x, a_predictedBounds.min.y },
		{ a_predictedBounds.min.x, a_predictedBounds.max.y },
		{ a_predictedBounds.max.x, a_predictedBounds.min.y },
		{ a_predictedBounds.max.x, a_predictedBounds.max.y }
	};

	std::sort(
		points.begin(),
		points.end(),
		[]( const RE::NiPoint2& a_left, const RE::NiPoint2& a_right )
		{
			return a_left.x < a_right.x || a_left.x == a_right.x && a_left.y < a_right.y;
		} );

	points.erase(
		std::unique(
			points.begin(),
			points.end(),
			[]( const RE::NiPoint2& a_left, const RE::NiPoint2& a_right )
			{
				return a_left.x == a_right.x && a_left.y == a_right.y;
			} ),
		points.end() );

	if( points.size() < 3 )
		return result;

	result.points.resize( points.size() * 2 );
	size_t hullSize = 0;

	for( const auto& point : points )
	{
		while( hullSize >= 2 && Cross2D( result.points[ hullSize - 2 ], result.points[ hullSize - 1 ], point ) <= 0 )
			--hullSize;

		result.points[ hullSize++ ] = point;
	}

	size_t lowerHullSize = hullSize;
	for( auto iter = points.rbegin() + 1; iter != points.rend(); ++iter )
	{
		while( hullSize > lowerHullSize && Cross2D( result.points[ hullSize - 2 ], result.points[ hullSize - 1 ], *iter ) <= 0 )
			--hullSize;

		result.points[ hullSize++ ] = *iter;
	}

	if( hullSize > 1 )
		--hullSize;

	result.points.resize( hullSize );
	if( result.points.size() < 3 )
	{
		result.points.clear();
		return result;
	}

	result.bounds.min = result.points.front();
	result.bounds.max = result.points.front();

	for( const auto& point : result.points )
	{
		result.bounds.min.x = std::min( result.bounds.min.x, point.x );
		result.bounds.min.y = std::min( result.bounds.min.y, point.y );
		result.bounds.max.x = std::max( result.bounds.max.x, point.x );
		result.bounds.max.y = std::max( result.bounds.max.y, point.y );
	}

	result.bounds.valid = true;
	result.valid = true;
	return result;
}

ProjectedBounds TargetSelection::ExtendProjectedBoundsUpwardForGravity( const RE::NiCamera* a_camera, const ProjectedBounds& a_bounds, const RE::NiPoint3& a_fireOrigin, const RE::NiPoint3& a_targetPosition, float a_projectileSpeed, float a_projectileGravity )
{
	ProjectedBounds result = a_bounds;

	if( !result.valid || !a_camera || a_projectileSpeed <= 0.0f || a_projectileGravity <= 0.0f )
		return result;

	RE::NiPoint3 delta = a_targetPosition - a_fireOrigin;
	float horizontalDistanceSquared = delta.x * delta.x + delta.y * delta.y;

	if( horizontalDistanceSquared <= 0.000001f )
		return result;

	float speedSquared = a_projectileSpeed * a_projectileSpeed;
	float discriminant =
		speedSquared * speedSquared -
		a_projectileGravity *
		( a_projectileGravity * horizontalDistanceSquared + 2.0f * delta.z * speedSquared );

	if( discriminant <= 0.0f )
		return result;

	float horizontalDistance = sqrt( horizontalDistanceSquared );
	float tangent =
		( speedSquared - sqrt( discriminant ) ) /
		( a_projectileGravity * horizontalDistance );
	RE::NiPoint3 compensatedPosition = a_targetPosition;
	compensatedPosition.z = a_fireOrigin.z + horizontalDistance * tangent;

	RE::NiBound targetPoint;
	targetPoint.center = a_targetPosition;
	targetPoint.radius = 0.0f;
	RE::NiPoint3 targetMin, targetMax;
	Native::ProjectBoundToScreen( a_camera, targetPoint, targetMin, targetMax, 0.0f );

	RE::NiBound compensatedPoint;
	compensatedPoint.center = compensatedPosition;
	compensatedPoint.radius = 0.0f;
	RE::NiPoint3 compensatedMin, compensatedMax;
	Native::ProjectBoundToScreen( a_camera, compensatedPoint, compensatedMin, compensatedMax, 0.0f );

	float upwardExtension = compensatedMin.y - targetMin.y;
	if( upwardExtension > 0.0f )
		result.max.y += upwardExtension * friendlyGravityAimErrorMultiplier;

	return result;
}

ProjectedBounds TargetSelection::GetHorizontallyInsetBounds( const ProjectedBounds& a_bounds, float a_inset )
{
	ProjectedBounds result = a_bounds;

	if( !result.valid )
		return result;

	float xInset = ( result.max.x - result.min.x ) * a_inset;
	result.min.x += xInset;
	result.max.x -= xInset;

	if( result.min.x > result.max.x )
		result.valid = false;

	return result;
}

bool TargetSelection::IsScreenCenterInsideProjectedSweep( const ProjectedSweep& a_sweep )
{
	if( !a_sweep.valid || a_sweep.points.size() < 3 )
		return false;

	RE::NiPoint2 center{ 0, 0 };
	bool hasNegative = false;
	bool hasPositive = false;

	for( size_t i = 0; i < a_sweep.points.size(); ++i )
	{
		float cross = Cross2D( a_sweep.points[ i ], a_sweep.points[ ( i + 1 ) % a_sweep.points.size() ], center );
		hasNegative |= cross < 0;
		hasPositive |= cross > 0;

		if( hasNegative && hasPositive )
			return false;
	}

	return true;
}

std::pair<RE::TESObjectREFR*, RE::NiPoint3> TargetSelection::GetPotentialPlayerTarget( float a_distance, float a_projectileSpeed, float a_projectileGravity, const RE::NiPoint3* a_fireOrigin, float a_xLimit, float a_yLimit, CandidateSelectionInfo* out_debugInfo, bool a_fixedPitchMode )
{
	if( a_xLimit <= 0 ) a_xLimit = 0.0001f;
	if( a_yLimit <= 0 ) a_yLimit = 0.0001f;

	float yLimitUp = a_yLimit;
	float yLimitDown = a_fixedPitchMode ? a_yLimit * 2.0f : a_yLimit;

	if( out_debugInfo )
		out_debugInfo->Reset( a_xLimit, yLimitUp, yLimitDown );

	auto processLists = RE::ProcessLists::GetSingleton();
	auto player = RE::PlayerCharacter::GetSingleton();
	RE::NiCamera* camera = RE::Main::WorldRootCamera();

	if( !processLists || !player || !camera )
		return {};

	float distanceSquared = a_distance * a_distance;
	float bestScore = FLT_MAX;
	float bestDistanceSquared = FLT_MAX;

	RE::NiPoint3 origin = a_fireOrigin ? *a_fireOrigin : player->GetPosition();

	RE::Actor* targetCandidate = nullptr;
	RE::NiPoint3 targetMoveDelta{ 0, 0, 0 };
	bool bestContainsCrosshair = false;
	bool isPlayerInCombat = player->IsInCombat();
	for( auto highHandle : processLists->highActorHandles )
	{
		if( auto highActor = highHandle.get() )
		{
			if( highActor->IsDead() )
				continue;

			auto actor3D = highActor->Get3D2();
			if( !actor3D )
				continue;

			if( actor3D->flags.any( RE::NiAVObject::Flag::kNotVisible ) )
				continue;

			bool isHostile = highActor->IsHostileToActor( player );
			if( isPlayerInCombat && !isHostile )
				continue;

			auto commander = highActor->GetCommandingActor();
			if( commander && commander.get() == player )
				continue;

			RE::NiPoint3 delta = origin - highActor->GetPosition();
			float actorDistanceSquared = delta.SqrLength();
			if( actorDistanceSquared > distanceSquared )
				continue;

			if( highActor->IsPlayerTeammate() )
				continue;

			RE::NiBound bound = actor3D->worldBound;
			RE::NiPoint3 moveDelta{ 0, 0, 0 };
			float ttr = 0;

			// Predict future position before projecting onto the screen
			if( a_projectileSpeed > 0 )
			{
				RE::NiPoint3 velocity;
				highActor->GetLinearVelocity( velocity );

				// Simple two pass predictor
				ttr = Ballistics::CalculateTimeToTarget( origin, bound.center, a_projectileSpeed );
				ttr = Ballistics::CalculateTimeToTarget( origin, bound.center + velocity * ttr, a_projectileSpeed );

				moveDelta = velocity * ttr;
			}

			auto currentBounds = ProjectReferenceBoundsToScreen( camera, highActor.get(), 0, 0 );
			auto projectedBounds = ttr > 0 ?
				ProjectReferenceBoundsToScreen( camera, highActor.get(), ttr, 0 ) :
				currentBounds;

			auto projectedSweep = GetProjectedSweep( currentBounds, projectedBounds );

			RE::NiPoint3 currentTargetPosition = bound.center;
			currentTargetPosition.x = highActor->GetPositionX();
			currentTargetPosition.y = highActor->GetPositionY();

			RE::NiPoint3 predictedTargetPosition = currentTargetPosition + moveDelta;
			auto friendlyCurrentBounds = GetHorizontallyInsetBounds( currentBounds, 0.1f );
			auto friendlyProjectedBounds = GetHorizontallyInsetBounds( projectedBounds, 0.1f );

			if( a_fixedPitchMode )
			{
				friendlyCurrentBounds = ExtendProjectedBoundsUpwardForGravity(
					camera, friendlyCurrentBounds, origin, currentTargetPosition, a_projectileSpeed, a_projectileGravity );

				friendlyProjectedBounds = ExtendProjectedBoundsUpwardForGravity(
					camera, friendlyProjectedBounds, origin, predictedTargetPosition, a_projectileSpeed, a_projectileGravity );
			}

			auto friendlyProjectedSweep = GetProjectedSweep( friendlyCurrentBounds, friendlyProjectedBounds );
			auto& projectedMin = projectedBounds.min;
			auto& projectedMax = projectedBounds.max;

			float hScore = abs( projectedMin.x + projectedMax.x ) / a_xLimit;
			float vScore = abs( projectedMin.y + projectedMax.y ) / a_yLimit;
			float score = hScore * hScore + vScore * vScore;

			TargetCandidateInfo* debugCandidate = nullptr;
			if( out_debugInfo )
			{
				out_debugInfo->candidates.push_back(
					{
						highActor->GetHandle(),
						bound.radius,
						highActor->GetBoundMax().x,
						score,
						isHostile
					} );

				debugCandidate = &out_debugInfo->candidates.back();
			}

			bool validProjection = projectedBounds.valid;

			if( debugCandidate )
				debugCandidate->validProjection = validProjection;
			else if( !validProjection )
				continue;

			bool insideSelectionWindow =
				projectedSweep.valid &&
				projectedSweep.bounds.max.x >= -a_xLimit && projectedSweep.bounds.min.x <= a_xLimit &&
				projectedSweep.bounds.max.y >= -yLimitDown && projectedSweep.bounds.min.y <= yLimitUp;

			if( debugCandidate )
				debugCandidate->insideSelectionWindow = insideSelectionWindow;
			else if( !insideSelectionWindow )
				continue;

			bool passedFriendlySelection = true;
			if( !isHostile )
				passedFriendlySelection = IsScreenCenterInsideProjectedSweep( friendlyProjectedSweep );

			if( debugCandidate )
				debugCandidate->passedFriendlySelection = passedFriendlySelection;
			else if( !passedFriendlySelection )
				continue;

			bool containsCrosshair = IsScreenCenterInsideProjectedSweep( projectedSweep );

			if( !isHostile )
				containsCrosshair = IsScreenCenterInsideProjectedSweep( friendlyProjectedSweep );

			if( !debugCandidate )
			{
				if( bestContainsCrosshair && !containsCrosshair )
					continue;

				if( containsCrosshair == bestContainsCrosshair )
				{
					if( containsCrosshair && actorDistanceSquared >= bestDistanceSquared )
						continue;

					if( !containsCrosshair && score >= bestScore )
						continue;
				}
			}

			bool hasLineOfSight = false;

			if( debugCandidate || validProjection && insideSelectionWindow && passedFriendlySelection )
				hasLineOfSight = HasLineOfSight( player, highActor.get() );

			if( debugCandidate )
				debugCandidate->hasLineOfSight = hasLineOfSight;

			bool eligible =
				validProjection &&
				insideSelectionWindow &&
				passedFriendlySelection &&
				hasLineOfSight;

			if( !eligible )
				continue;

			bool isBetterCandidate = false;

			if( containsCrosshair != bestContainsCrosshair )
				isBetterCandidate = containsCrosshair;
			else if( containsCrosshair )
				isBetterCandidate = actorDistanceSquared < bestDistanceSquared;
			else
				isBetterCandidate = score < bestScore;

			if( !isBetterCandidate )
				continue;

			bestContainsCrosshair = containsCrosshair;
			bestDistanceSquared = actorDistanceSquared;
			bestScore = score;
			targetCandidate = highActor.get();
			targetMoveDelta = moveDelta;
		}
	}

	if( out_debugInfo && targetCandidate )
	{
		auto targetHandle = targetCandidate->GetHandle();

		for( auto& candidate : out_debugInfo->candidates )
		{
			if( candidate.handle == targetHandle )
			{
				candidate.selected = true;
				break;
			}
		}
	}

	if( targetCandidate )
		return std::make_pair( targetCandidate, targetCandidate->GetPosition() + targetMoveDelta );

	return std::make_pair( nullptr, targetMoveDelta );
}
