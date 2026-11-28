#pragma once
#include <shared_mutex>
#include "ActorState.h"

struct ProjectilePredictionResult
{
	enum Type
	{
		kNotSupported,
		kImpacted,
		kDestroyed,
		kOutOfRange,
		kFlightTimeTooLong,
	};

	Type						type			= kNotSupported;
	float						distanceMoved	= 0;
	float						flightTime		= 0;
	RE::NiPoint3				location		= { 0, 0, 0 };
	RE::Projectile::ImpactData	impact;

	ProjectilePredictionResult()
	{
		memset( &impact, 0, sizeof(RE::Projectile::ImpactData) );
	}
};

namespace ProjectilePrediction
{
	constexpr float simulationStep = 1.0f / 30.0f; // 1/30 second time step
	struct PredictorPool
	{
		// This can be set to 5 for cardinal direction collision test.
		// But runtime testing showed it no longer necessary.
		// The inaccuracy comes from the actual projectile spawn origin is different from the ready-to-fire origin because of the firing animation moved it before release.
		static constexpr size_t kPredictorCount = 1;
		RE::Projectile* predictors[kPredictorCount];
		RE::Projectile*& operator[]( size_t a_index )
		{
			return predictors[ a_index ];
		}
	};

	class PredictorPoolManager
	{
	public:
		static inline std::mutex poolMutex;
		static PredictorPoolManager* GetSingleton();
		PredictorPool* GetPredictors( const RE::Projectile::LaunchData& a_launchData, RE::CFilter a_collisionFilter, const RE::NiPoint3* a_offsets );

		void ReleasePredictor( PredictorPool* a_pool );
		void Invalidate();

		void Destroy();
	private:
		static constexpr size_t _poolCount = 2;
		PredictorPool		_predictorPool[_poolCount]	= { nullptr, nullptr };
		RE::BGSProjectile*	_predictorKey[_poolCount]	= { nullptr, nullptr };
		bool _inUse[2] = {false, false};
	};

	class PredictorPoolLease
	{
	public:
		PredictorPoolLease( const RE::Projectile::LaunchData& a_launchData, RE::CFilter a_collisionFilter, RE::NiPoint3* a_offsets )
		{
			_pool = PredictorPoolManager::GetSingleton()->GetPredictors( a_launchData, a_collisionFilter, a_offsets ? a_offsets : &RE::NiPoint3::Zero() );
		}

		~PredictorPoolLease()
		{
			if( _pool )
			{
				PredictorPoolManager::GetSingleton()->ReleasePredictor( _pool );
			}
		}

		PredictorPoolLease( const PredictorPoolLease& ) = delete;
		PredictorPoolLease& operator=( const PredictorPoolLease& ) = delete;

		explicit operator bool() const { return _pool != nullptr; }
		PredictorPool& operator*() const { return *_pool; }
		PredictorPool* operator->() const { return _pool; }
	private:
		PredictorPool* _pool = nullptr;
	};

	class Predictor
	{
	public:
		Predictor( const RE::Projectile::LaunchData& a_launchData, RE::CFilter a_collisionFilter ) :
			_lease( a_launchData, a_collisionFilter, nullptr )
		{}

		RE::Projectile* GetPredictorProjectile()
		{
			return _lease ? (*_lease)[0] : nullptr;
		}

		ProjectilePredictionResult Predict( const RE::Projectile::LaunchData& a_launchData );
	private:
		PredictorPoolLease _lease;
	};

	void CleanUpPredictorCache();
	void InvalidatePredictorCache();
	bool IsSimulatingPredictor();

	class OriginCalibration
	{
	public:
		struct Sample
		{
			float pitch{ 0.0f };
			float y{ 0.0f };
			float z{ 0.0f };
			uint32_t count{ 0 };
		};

		static OriginCalibration* GetSingleton()
		{
			static OriginCalibration singleton;
			return &singleton;
		}

		void Clear()
		{
			std::unique_lock lock( _lock );

			_samples.clear();
			_localX = 0.0f;
			_xValid = false;
		}

		void AddSample( const RE::NiPoint3& a_localOrigin, float a_pitch )
		{
			if( !IsValid( a_localOrigin ) || !isfinite( a_pitch ) )
				return;

			std::unique_lock lock( _lock );

			// X is not pitch-dependent. Keep the latest authoritative value.
			_localX = a_localOrigin.x;
			_xValid = true;

			auto upper = LowerBound( a_pitch );

			Sample* matchingSample = nullptr;

			if( upper != _samples.end() && abs( upper->pitch - a_pitch ) <= kMergePitchRange )
			{
				matchingSample = &*upper;
			}
			else if( upper != _samples.begin() )
			{
				auto previous = upper - 1;

				if( abs( previous->pitch - a_pitch ) <= kMergePitchRange )
					matchingSample = &*previous;
			}

			if( matchingSample )
			{
				// Keep the original pitch so averaging cannot break sort order.
				matchingSample->count++;

				const float weight = 1.0f / (float)matchingSample->count;

				matchingSample->y += ( a_localOrigin.y - matchingSample->y ) * weight;
				matchingSample->z += ( a_localOrigin.z - matchingSample->z ) * weight;

				//REX::DEBUG(
				//	"Origin calibration merged[ Pitch:{}, Y:{}, Z:{}, Count:{} ]",
				//	matchingSample->pitch,
				//	matchingSample->y,
				//	matchingSample->z,
				//	matchingSample->count );

				return;
			}

			_samples.insert(
				upper,
				{
					a_pitch,
					a_localOrigin.y,
					a_localOrigin.z,
					1
				} );

			//REX::DEBUG(
			//	"Origin calibration added[ Pitch:{}, Y:{}, Z:{}, Samples:{} ]",
			//	a_pitch,
			//	a_localOrigin.y,
			//	a_localOrigin.z,
			//	_samples.size() );
		}

		bool GetOrigin( const RE::NiTransform& a_playerTransform, float a_pitch, RE::NiPoint3& a_origin ) const
		{
			if( !isfinite( a_pitch ) )
				return false;

			std::shared_lock lock( _lock );

			if( !_xValid || _samples.empty() )
				return false;

			if( _samples.size() == 1 )
				return GetSingleSampleOrigin( a_playerTransform, a_pitch, a_origin );

			auto upper = LowerBound( a_pitch );

			const Sample* low = nullptr;
			const Sample* high = nullptr;
			bool extrapolated = false;

			if( upper == _samples.begin() )
			{
				low = &_samples[0];
				high = &_samples[1];
				extrapolated = a_pitch < low->pitch;
			}
			else if( upper == _samples.end() )
			{
				low = &_samples[_samples.size() - 2];
				high = &_samples.back();
				extrapolated = a_pitch > high->pitch;
			}
			else
			{
				low = &*( upper - 1 );
				high = &*upper;
			}

			const float sampleRange = high->pitch - low->pitch;

			if( sampleRange <= 0.0001f )
				return false;

			if( extrapolated )
			{
				// A narrow pair produces an unreliable slope.
				if( sampleRange < kMinimumExtrapolationSampleRange )
					return false;

				const float outOfRangeDistance =
					a_pitch < low->pitch ?
					low->pitch - a_pitch :
					a_pitch - high->pitch;

				// Outside this distance, let the caller use the manual fallback.
				if( outOfRangeDistance > kMaximumExtrapolationRange )
					return false;
			}

			const float factor = ( a_pitch - low->pitch ) / sampleRange;

			RE::NiPoint3 localOrigin{
				_localX,
				std::lerp( low->y, high->y, factor ),
				std::lerp( low->z, high->z, factor )
			};

			if( !IsValid( localOrigin ) )
				return false;

			a_origin = a_playerTransform * localOrigin;

			if( !IsValid( a_origin ) )
				return false;

			//REX::DEBUG(
			//	"Origin calibration {}[ Pitch:{}, Low:{}, High:{}, "
			//	"Factor:{}, Local:({}, {}, {}) ]",
			//	extrapolated ? "extrapolated" : "interpolated",
			//	a_pitch,
			//	low->pitch,
			//	high->pitch,
			//	factor,
			//	localOrigin.x,
			//	localOrigin.y,
			//	localOrigin.z );

			return true;
		}

		size_t GetSampleCount() const
		{
			std::shared_lock lock( _lock );
			return _samples.size();
		}

		bool GetPitchRange( float& a_minimumPitch, float& a_maximumPitch ) const
		{
			std::shared_lock lock( _lock );

			if( _samples.empty() )
				return false;

			a_minimumPitch = _samples.front().pitch;
			a_maximumPitch = _samples.back().pitch;
			return true;
		}

	private:
		using SampleIterator = std::vector<Sample>::iterator;
		using ConstSampleIterator = std::vector<Sample>::const_iterator;

		static constexpr float kMergePitchRange = 0.0174533f;               // 1 degree
		static constexpr float kSingleSampleRange = 0.0872665f;             // 5 degrees
		static constexpr float kMinimumExtrapolationSampleRange = 0.15f;    // 8.6 degrees
		static constexpr float kMaximumExtrapolationRange = 0.35f;          // 20 degrees

		SampleIterator LowerBound( float a_pitch )
		{
			return std::lower_bound(
				_samples.begin(),
				_samples.end(),
				a_pitch,
				[]( const Sample& a_sample, float a_value )
				{
					return a_sample.pitch < a_value;
				} );
		}

		ConstSampleIterator LowerBound( float a_pitch ) const
		{
			return std::lower_bound(
				_samples.begin(),
				_samples.end(),
				a_pitch,
				[]( const Sample& a_sample, float a_value )
				{
					return a_sample.pitch < a_value;
				} );
		}

		bool GetSingleSampleOrigin( const RE::NiTransform& a_playerTransform, float a_pitch, RE::NiPoint3& a_origin ) const
		{
			const auto& sample = _samples.front();

			if( abs( a_pitch - sample.pitch ) > kSingleSampleRange )
				return false;

			RE::NiPoint3 localOrigin{ _localX, sample.y, sample.z };

			a_origin = a_playerTransform * 	localOrigin;

			return IsValid( a_origin );
		}

		static bool IsValid( const RE::NiPoint3& a_point )
		{
			return 	isfinite( a_point.x ) && isfinite( a_point.y ) && isfinite( a_point.z );
		}

		mutable std::shared_mutex _lock;
		std::vector<Sample> _samples;

		float _localX{ 0.0f };
		bool _xValid{ false };
	};

	class MagicOriginCalibrationManager
	{
	public:
		static MagicOriginCalibrationManager* GetSingleton()
		{
			static MagicOriginCalibrationManager singleton;
			return &singleton;
		}

		void Clear()
		{
			for( auto& calibration : _calibrations )
				calibration.Clear();
		}

		void Clear( RE::MagicSystem::CastingSource a_castingSource, bool a_isSneaking )
		{
			auto* calibration = GetCalibration( a_castingSource, a_isSneaking );

			if( calibration )
				calibration->Clear();
		}

		bool AddSample( RE::MagicSystem::CastingSource a_castingSource, bool a_isSneaking, const RE::NiPoint3& a_localOrigin, float a_pitch )
		{
			auto* calibration = GetCalibration( a_castingSource, a_isSneaking );

			if( !calibration )
				return false;

			calibration->AddSample( a_localOrigin, a_pitch );

			return true;
		}

		bool GetOrigin( RE::MagicSystem::CastingSource a_castingSource, bool a_isSneaking, const RE::NiTransform& a_playerTransform, float a_pitch, RE::NiPoint3& a_origin ) const
		{
			const auto* calibration = GetCalibration( a_castingSource, a_isSneaking );

			if( !calibration )
				return false;

			return calibration->GetOrigin( a_playerTransform, a_pitch, a_origin );
		}

		size_t GetSampleCount( RE::MagicSystem::CastingSource a_castingSource, bool a_isSneaking ) const
		{
			const auto* calibration = GetCalibration( a_castingSource, a_isSneaking );

			return calibration ? calibration->GetSampleCount() : 0;
		}

		bool GetPitchRange( RE::MagicSystem::CastingSource a_castingSource, bool a_isSneaking, float& a_minimumPitch, float& a_maximumPitch ) const
		{
			const auto* calibration = GetCalibration( a_castingSource, a_isSneaking );

			if( !calibration )
				return false;

			return calibration->GetPitchRange( a_minimumPitch, a_maximumPitch );
		}

	private:
		enum class Index : size_t
		{
			kLeftStanding,
			kLeftSneaking,
			kRightStanding,
			kRightSneaking,

			kTotal
		};

		static std::optional<Index> GetIndex( RE::MagicSystem::CastingSource a_castingSource, bool a_isSneaking )
		{
			switch( a_castingSource )
			{
			case RE::MagicSystem::CastingSource::kLeftHand:
				return a_isSneaking ?
					Index::kLeftSneaking :
					Index::kLeftStanding;

			case RE::MagicSystem::CastingSource::kRightHand:
				return a_isSneaking ?
					Index::kRightSneaking :
					Index::kRightStanding;

			default:
				return std::nullopt;
			}
		}

		OriginCalibration* GetCalibration( RE::MagicSystem::CastingSource a_castingSource, bool a_isSneaking )
		{
			auto index = GetIndex( a_castingSource, a_isSneaking );

			if( !index )
				return nullptr;

			return &_calibrations[(size_t)*index];
		}

		const OriginCalibration* GetCalibration( RE::MagicSystem::CastingSource a_castingSource, bool a_isSneaking ) const
		{
			auto index = GetIndex( a_castingSource, a_isSneaking );

			if( !index )
				return nullptr;

			return &_calibrations[(size_t)*index];
		}

		std::array< OriginCalibration,(size_t)Index::kTotal> _calibrations;
	};
}
