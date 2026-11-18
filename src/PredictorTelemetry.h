#pragma once
#pragma once

#include "PCH.h"
#include "ProjectilePrediction.h"

namespace ProjectilePrediction
{
	class PredictorTelemetry
	{
	public:
		static PredictorTelemetry* GetSingleton()
		{
			static PredictorTelemetry singleton;
			return &singleton;
		}

#ifdef _DEBUG
		void RecordPrediction( const ProjectilePredictionResult& a_result )
		{
			uint64_t predictionCount = ++_predictionCount;

			switch( a_result.type )
			{
			case ProjectilePredictionResult::kImpacted:
				++_impactCount;
				break;

			case ProjectilePredictionResult::kDestroyed:
				++_destroyedCount;
				break;

			case ProjectilePredictionResult::kOutOfRange:
				++_outOfRangeCount;
				break;

			case ProjectilePredictionResult::kFlightTimeTooLong:
				++_flightTimeTooLongCount;
				break;

			case ProjectilePredictionResult::kNotSupported:
				++_notSupportedCount;
				break;

			default:
				break;
			}

			if( predictionCount % reportInterval == 0 )
				Report();
		}

		void RecordPredictionStep()
		{
			++_predictionStepCount;
		}

		void RecordResetFailure()
		{
			++_resetFailureCount;
		}

		void RecordPoolCreation()
		{
			++_poolCreationCount;
		}

		void RecordPoolCreationFailure()
		{
			++_poolCreationFailureCount;
		}

		void RecordPoolReplacement()
		{
			++_poolReplacementCount;
		}

		void RecordPredictorDestruction()
		{
			++_predictorDestructionCount;
		}

		void RecordNativeTeardownAttempt()
		{
			++_nativeTeardownAttemptCount;
		}

		void RecordPredictionWithNativeTeardown()
		{
			++_nativePredictionTeardownAttemptCount;
		}
		
		void RecordReleaseCacheCapture()
		{
			++_releaseCacheCaptureCount;
		}

		void RecordReleaseCacheUse()
		{
			++_releaseCacheUseCount;
		}

		void RecordReleaseCacheExpiration()
		{
			++_releaseCacheExpirationCount;
		}

		void RecordReleaseConsumption()
		{
			++_releaseConsumptionCount;
		}

		void AcquireLease()
		{
			uint32_t activeLeases = _activeLeaseCount.fetch_add( 1 ) + 1;
			uint32_t maximumLeases = _maximumActiveLeaseCount.load();

			while(
				activeLeases > maximumLeases &&
				!_maximumActiveLeaseCount.compare_exchange_weak( maximumLeases, activeLeases ) )
			{}
		}

		void ReleaseLease()
		{
			uint32_t previousLeaseCount = _activeLeaseCount.fetch_sub( 1 );

			if( previousLeaseCount == 0 )
			{
				_activeLeaseCount = 0;
				++_leaseUnderflowCount;
			}
		}

		void Report() const
		{
			uint64_t predictionCount = _predictionCount.load();
			uint64_t stepCount = _predictionStepCount.load();

			double averageStepCount = predictionCount > 0 ?
				(double)stepCount / (double)predictionCount :
				0.0;

			REX::DEBUG(
				"Predictor Telemetry[ "
				"Predictions:{}, "
				"Steps:{}, "
				"AverageSteps:{:.2f}, "
				"Impacts:{}, "
				"Destroyed:{}, "
				"OutOfRange:{}, "
				"Timeout:{}, "
				"NotSupported:{}, "
				"ResetFailures:{}, "
				"PoolCreations:{}, "
				"PoolCreationFailures:{}, "
				"PoolReplacements:{}, "
				"PredictorDestructions:{}, "
				"NativeTeardownAttempts:{}, "
				"PredictionsWithNativeTeardown:{}, "
				"ActiveLeases:{}, "
				"MaximumActiveLeases:{}, "
				"LeaseUnderflows:{}, "
				"ReleaseCaptures:{}, "
				"ReleaseUses:{}, "
				"ReleaseExpirations:{}, "
				"ReleaseConsumed:{} ]",
				predictionCount,
				stepCount,
				averageStepCount,
				_impactCount.load(),
				_destroyedCount.load(),
				_outOfRangeCount.load(),
				_flightTimeTooLongCount.load(),
				_notSupportedCount.load(),
				_resetFailureCount.load(),
				_poolCreationCount.load(),
				_poolCreationFailureCount.load(),
				_poolReplacementCount.load(),
				_predictorDestructionCount.load(),
				_nativeTeardownAttemptCount.load(),
				_nativePredictionTeardownAttemptCount.load(),
				_activeLeaseCount.load(),
				_maximumActiveLeaseCount.load(),
				_leaseUnderflowCount.load(),
				_releaseCacheCaptureCount.load(),
				_releaseCacheUseCount.load(),
				_releaseCacheExpirationCount.load(),
				_releaseConsumptionCount.load() );
		}

		void Reset()
		{
			_predictionCount = 0;
			_predictionStepCount = 0;
			_impactCount = 0;
			_destroyedCount = 0;
			_outOfRangeCount = 0;
			_flightTimeTooLongCount = 0;
			_notSupportedCount = 0;
			_resetFailureCount = 0;
			_poolCreationCount = 0;
			_poolCreationFailureCount = 0;
			_poolReplacementCount = 0;
			_predictorDestructionCount = 0;
			_nativeTeardownAttemptCount = 0;
			_nativePredictionTeardownAttemptCount = 0;
			_activeLeaseCount = 0;
			_maximumActiveLeaseCount = 0;
			_leaseUnderflowCount = 0;
			_releaseCacheCaptureCount = 0;
			_releaseCacheUseCount = 0;
			_releaseCacheExpirationCount = 0;
			_releaseConsumptionCount = 0;
		}
#else
		void RecordPrediction( const ProjectilePredictionResult& ) {}
		void RecordPredictionStep() {}
		void RecordResetFailure() {}
		void RecordPoolCreation() {}
		void RecordPoolCreationFailure() {}
		void RecordPoolReplacement() {}
		void RecordPredictorDestruction() {}
		void RecordNativeTeardownAttempt() {}
		void RecordReleaseCacheCapture() {}
		void RecordReleaseCacheUse() {}
		void RecordReleaseCacheExpiration() {}
		void AcquireLease() {}
		void ReleaseLease() {}
		void Report() const {}
		void Reset() {}
#endif

	private:
		PredictorTelemetry() = default;
		PredictorTelemetry( const PredictorTelemetry& ) = delete;
		PredictorTelemetry( PredictorTelemetry&& ) = delete;

		PredictorTelemetry& operator=( const PredictorTelemetry& ) = delete;
		PredictorTelemetry& operator=( PredictorTelemetry&& ) = delete;

#ifdef _DEBUG
		static constexpr uint64_t reportInterval = 10000;

		std::atomic<uint64_t> _predictionCount = 0;
		std::atomic<uint64_t> _predictionStepCount = 0;
		std::atomic<uint64_t> _impactCount = 0;
		std::atomic<uint64_t> _destroyedCount = 0;
		std::atomic<uint64_t> _outOfRangeCount = 0;
		std::atomic<uint64_t> _flightTimeTooLongCount = 0;
		std::atomic<uint64_t> _notSupportedCount = 0;
		std::atomic<uint64_t> _resetFailureCount = 0;
		std::atomic<uint64_t> _poolCreationCount = 0;
		std::atomic<uint64_t> _poolCreationFailureCount = 0;
		std::atomic<uint64_t> _poolReplacementCount = 0;
		std::atomic<uint64_t> _predictorDestructionCount = 0;
		std::atomic<uint64_t> _nativeTeardownAttemptCount = 0;
		std::atomic<uint64_t> _nativePredictionTeardownAttemptCount = 0;
		std::atomic<uint32_t> _activeLeaseCount = 0;
		std::atomic<uint32_t> _maximumActiveLeaseCount = 0;
		std::atomic<uint64_t> _leaseUnderflowCount = 0;
		std::atomic<uint64_t> _releaseCacheCaptureCount = 0;
		std::atomic<uint64_t> _releaseCacheUseCount = 0;
		std::atomic<uint64_t> _releaseCacheExpirationCount = 0;
		std::atomic<uint64_t> _releaseConsumptionCount = 0;
#endif
	};

	class PredictorTelemetryLease
	{
	public:
		PredictorTelemetryLease()
		{
			PredictorTelemetry::GetSingleton()->AcquireLease();
		}

		~PredictorTelemetryLease()
		{
			PredictorTelemetry::GetSingleton()->ReleaseLease();
		}

		PredictorTelemetryLease( const PredictorTelemetryLease& ) = delete;
		PredictorTelemetryLease( PredictorTelemetryLease&& ) = delete;

		PredictorTelemetryLease& operator=( const PredictorTelemetryLease& ) = delete;
		PredictorTelemetryLease& operator=( PredictorTelemetryLease&& ) = delete;
	};
}
