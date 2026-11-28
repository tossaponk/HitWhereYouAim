#pragma once
#define NOMINMAX
#define NOGDI
#define WIN32_LEAN_AND_MEAN
//#define HWYA_PROFILER_ACTIVE

#include <RE/Skyrim.h>
#include <REX/REX.h>
#include <REX/LOG.h>
#include <SKSE/SKSE.h>

#ifdef NDEBUG
#	include <spdlog/sinks/basic_file_sink.h>
#else
#	include <spdlog/sinks/msvc_sink.h>
#endif

#include <xbyak\xbyak.h>
#include <SimpleIni.h>
#include <MinHook.h>

using namespace std::literals;

namespace Runtime
{
	inline constexpr REL::Version SSE_1_7_99(1, 7, 99, 0);
	inline constexpr REL::Version MIN_ADDRESS_LIBRARY_V5 = SSE_1_7_99;

	inline REL::Version version{};

	[[nodiscard]] inline bool IsAtLeast1_7_99() noexcept
	{
		return version >= Runtime::SSE_1_7_99;
	}
}

namespace stl
{
	template <class F, class T>
	void write_thunk_call_virtual(std::uintptr_t a_src, uint64_t a_index, uint64_t a_slot)
	{
		REL::Relocation<std::uintptr_t> vtbl{ F::VTABLE[a_index] };
		const auto addr = vtbl.address() + ( sizeof(void*) * a_slot );
		const auto result = *reinterpret_cast<std::uintptr_t*>(addr);

		auto& trampoline = SKSE::GetTrampoline();
		trampoline.write_call<6>(a_src, T::thunk);

		T::func = result;
	}

	template <class T, std::size_t N = 5>
	void write_thunk_call(std::uintptr_t a_src)
	{
		if( a_src == 0 )
			REX::FAIL( "ERROR: Trying to write a call to NULL address!" );
		auto& trampoline = SKSE::GetTrampoline();

		T::func = trampoline.write_call<N>(a_src, T::thunk);
	}

	template <class T, std::size_t N = 5>
	void write_thunk_branch(std::uintptr_t a_src)
	{
		auto& trampoline = SKSE::GetTrampoline();

		T::func = trampoline.write_branch<N>(a_src, T::thunk);
	}

	template <class T>
	void write_patch_call( std::uintptr_t a_src, Xbyak::CodeGenerator& a_code )
	{
		auto& trampoline = SKSE::GetTrampoline();
		auto target = trampoline.allocate( a_code );

		T::func = trampoline.write_call<5>( a_src, target );
	}

	template <class T>
	void write_patch_branch( std::uintptr_t a_src, Xbyak::CodeGenerator& a_code )
	{
		auto& trampoline = SKSE::GetTrampoline();
		auto target = trampoline.allocate( a_code );

		T::func = trampoline.write_branch<5>( a_src, target );
	}

	template <class F, size_t index, class T>
	void write_vfunc()
	{
		REL::Relocation<std::uintptr_t> vtbl{ F::VTABLE[index] };
		T::func = vtbl.write_vfunc(T::size, T::thunk);
	}

	template <class F, class T>
	void write_vfunc()
	{
		write_vfunc<F, 0, T>();
	}
}

namespace util
{
	struct PatternToken
	{
		bool wildcard;
		uint8_t value;
	};

	inline std::vector<PatternToken> ParsePattern( std::string_view a_pattern )
	{
		std::vector<PatternToken> result;

		for( size_t i = 0; i < a_pattern.size(); )
		{
			while( i < a_pattern.size() && isspace( (unsigned char)a_pattern[ i ] ) )
			{
				++i;
			}

			if( i >= a_pattern.size() )
			{
				break;
			}

			if( a_pattern[ i ] == '?' )
			{
				result.push_back( { true, 0 } );

				if( i + 1 < a_pattern.size() && a_pattern[ i + 1 ] == '?' )
				{
					++i;
				}

				++i;
				continue;
			}

			unsigned int value = 0;

			std::from_chars(
				a_pattern.data() + i,
				a_pattern.data() + i + 2,
				value,
				16 );

			result.push_back(
				{
					false,
					(uint8_t)value
				} );

			i += 2;
		}

		return result;
	}

	inline uintptr_t FindPatternInFunction( uintptr_t a_function, std::string_view a_pattern, size_t a_maxScan = 0x4000, bool a_stopAtPadding = true )
	{
		auto pattern = ParsePattern( a_pattern );

		if( pattern.empty() )
		{
			return 0;
		}

		auto ptr = (uint8_t*)a_function;

		while( (uintptr_t)ptr < a_function + a_maxScan )
		{
			if( a_stopAtPadding && *(uint32_t*)ptr == 0xCCCCCCC3 )
			{
				return 0;
			}

			bool match = true;

			for( size_t i = 0; i < pattern.size(); ++i )
			{
				if( !pattern[ i ].wildcard && ptr[ i ] != pattern[ i ].value )
				{
					match = false;
					break;
				}
			}

			if( match )
			{
				return (uintptr_t)ptr;
			}

			++ptr;
		}

		return 0;
	}

	inline uintptr_t ResolveCallTarget( uintptr_t a_call )
	{
		auto offset = *(int32_t*)( a_call + 1 );

		return a_call + 5 + offset;
	}

	static uintptr_t GetInjectionAddress( uint64_t a_relocID, const char* a_pattern, uint64_t a_offset = 0, const char* a_targetInstruction = NULL )
	{
		auto addr = FindPatternInFunction( REL::Relocation<uintptr_t>( REL::ID(a_relocID) ).address(), a_pattern );
		if( addr )
		{
			addr += a_offset;
			if( a_targetInstruction )
			{
				auto targetCheck = FindPatternInFunction( addr, a_targetInstruction, 6 );
				if( targetCheck != addr )
					return NULL;
			}

			return addr;
		}

		return NULL;
	}

	static uintptr_t GetInjectionAddress( REL::RelocationID a_relocation, const char* a_pattern, uint64_t a_offset = 0, const char* a_targetInstruction = NULL )
	{
		auto addr = FindPatternInFunction( a_relocation.address(), a_pattern );
		if( addr )
		{
			addr += a_offset;
			if( a_targetInstruction )
			{
				auto targetCheck = FindPatternInFunction( addr, a_targetInstruction, 6 );
				if( targetCheck != addr )
					return NULL;
			}

			return addr;
		}

		return NULL;
	}
}

class Profiler
{
public:
	enum Component : uint8_t
	{
		kAimPitch,
		kAimValidation,
		kResolveCrosshairAim,
		kTargetPrediction,
		kProjectilePrediction,
		kRaycastValidation,
		kCrosshairUpdate,
		kTrailUpdate,
		kTotal
	};

	class Scope
	{
	public:
		explicit Scope( Component a_component ) :
			component( a_component ),
			start( Clock::now() )
		{}

		~Scope()
		{
			auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>( Clock::now() - start ).count();
			auto& data = components[ component ];

			data.total.fetch_add( elapsed, std::memory_order_relaxed );
			data.count.fetch_add( 1, std::memory_order_relaxed );

			auto maximum = data.maximum.load( std::memory_order_relaxed );
			while( elapsed > maximum && !data.maximum.compare_exchange_weak( maximum, elapsed, std::memory_order_relaxed ) )
			{}
		}

	private:
		using Clock = std::chrono::steady_clock;

		Component component;
		Clock::time_point start;
	};

	static void Report()
	{
		for( size_t i = 0; i < kTotal; ++i )
		{
			auto count = components[ i ].count.exchange( 0, std::memory_order_relaxed );
			auto total = components[ i ].total.exchange( 0, std::memory_order_relaxed );
			auto maximum = components[ i ].maximum.exchange( 0, std::memory_order_relaxed );

			if( !count )
				continue;

			double totalMS = (double)total / 1'000'000.0;
			double averageUS = (double)total / (double)count / 1'000.0;
			double maximumUS = (double)maximum / 1'000.0;

			REX::INFO(
				"Profiler[ Component:{}, Calls:{}, Total:{:.3f} ms, Average:{:.3f} us, Maximum:{:.3f} us ]",
				names[ i ],
				count,
				totalMS,
				averageUS,
				maximumUS );
		}
	}

private:
	struct Data
	{
		std::atomic_int64_t total{ 0 };
		std::atomic_uint64_t count{ 0 };
		std::atomic_int64_t maximum{ 0 };
	};

	static inline std::array<Data, kTotal> components;
	static inline constexpr std::array names{
		"AimPitch",
		"AimValidation",
		"ResolveCrosshairAim",
		"TargetPrediction",
		"ProjectilePrediction",
		"RaycastValidation",
		"CrosshairUpdate",
		"TrailUpdate"
	};
};

constexpr uint64_t RELOCATION_RAW( const uint64_t se, const uint64_t ae )
{
	return Runtime::version <= SKSE::RUNTIME_SSE_1_5_97 ? se : ae;
}

enum class PitchCorrectionMode
{
	kAutomatic,
	kFixedAngle,
};

static constexpr float twoPi = 2.0f * std::numbers::pi_v<float>;
static constexpr float havokScaling = 0.0142875f;
static constexpr float degToRad = std::numbers::pi_v<float> / 180.0f;
static constexpr float radToDeg = 180.0f / std::numbers::pi_v<float>;
static constexpr float meterToGameUnit = 1.0f / 0.0142875f;
static constexpr float feetToGameUnit = 128.0f / 6.0f;
static constexpr float predictorDamage = -1.0f;
static constexpr float instantProjectileSpeed = 1.0e6f;
