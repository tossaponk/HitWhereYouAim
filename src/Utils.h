#pragma once
#pragma warning(push)

#include "Settings.h"
#include "Native.h"

template <class... Args>
static void PrintDebugInfo( std::format_string<Args...> a_format, Args&&... a_args )
{
	if( !Settings::bEnableLogging )
		return;

	std::string message = fmt::vformat( a_format.get(), fmt::make_format_args( a_args... ) );
	REX::DEBUG( message.c_str() );

	if( auto* console = RE::ConsoleLog::GetSingleton() )
		console->Print( message.c_str() );
}

inline std::vector<std::string> split( const char* a_input, const char* a_regex ) 
{
	// passing -1 as the submatch index parameter performs splitting
	std::string in(a_input);
	std::regex re(a_regex);
	std::sregex_token_iterator
		_first{in.begin(), in.end(), re, -1},
		_last;
	return {_first, _last};
}

inline uint32_t GetCurrentGameRuntimeMS()
{
	return  RE::BSTimer::GetSingleton()->smoothedRunTimeMS;
}

// Helper to get object reference from an AV object returned from raycast
inline RE::TESObjectREFR* GetObjectReferenceFromAVObject( RE::NiAVObject* a_object )
{
	if( a_object->userData )
		return a_object->userData;

	if( a_object->parent )
		return GetObjectReferenceFromAVObject( a_object->parent );

	return nullptr;
}

inline bool HasLineOfSight( RE::TESObjectREFR* a_observer, RE::TESObjectREFR* a_target )
{
	static auto hasLOSCondition = RE::TESCondition();
	static RE::TESConditionItem hasLOS = RE::TESConditionItem();
	hasLOS.data.comparisonValue.f = 1.0f;
	hasLOS.data.functionData.function = RE::FUNCTION_DATA::FunctionID::kGetLineOfSight;
	hasLOSCondition.head = &hasLOS;

	return hasLOSCondition.IsTrue( a_observer, a_target );
}

using conditionFunc_t = bool(RE::TESObjectREFR* a_ref, void* a_param1, void* a_param2, double& out_result);
inline conditionFunc_t* GetConditionFunctionEntry( RE::FUNCTION_DATA::FunctionID a_id )
{
	static auto functionTable = REL::Relocation<uintptr_t>( RELOCATION_ID( 501795, 361129 ) ).address();
	static size_t stride = 0x50;
	return *reinterpret_cast<conditionFunc_t**>( functionTable + (uint32_t)a_id * stride );
}

template<typename T=RE::hkVector4>
inline T HavokToWorld( const RE::hkVector4& a_coord )
{
	T result = (T&)a_coord;

	return result / havokScaling;
}

template<typename T=RE::hkVector4>
inline T WorldToHavok( const RE::hkVector4& a_coord )
{
	T result = (T&)a_coord;

	return result * havokScaling;
}

inline RE::BGSProjectile* GetMagicProjectile( const RE::MagicItem* a_magicItem )
{
	if( !a_magicItem )
		return nullptr;

	auto effect = a_magicItem->GetAVEffect();
	return effect ? effect->data.projectileBase : nullptr;
}

inline bool IsRangedMagic( RE::MagicItem* a_spell )
{
	switch( a_spell->GetDelivery() )
	{
	case RE::MagicSystem::Delivery::kAimed:
	case RE::MagicSystem::Delivery::kTargetActor:
	case RE::MagicSystem::Delivery::kTargetLocation:
		return true;
	default:
		return false;
	}
}


inline void DumpChangedAnimationGraphVariables( RE::IAnimationGraphManagerHolder* a_holder )
{
	if( !a_holder )
		return;

	RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
	if( !a_holder->GetAnimationGraphManager( manager ) || !manager )
		return;

	static RE::BSAnimationGraphManager* previousManager = nullptr;
	static std::unordered_map<std::string, std::uint32_t> previousValues;

	if( manager.get() != previousManager )
	{
		previousManager = manager.get();
		previousValues.clear();

		REX::DEBUG(
			"Animation graph manager changed: {:p}, graph count: {}",
			static_cast<void*>( previousManager ),
			manager->graphs.size()
		);
	}

	for( const auto& entry : manager->variableCache.variableCache )
	{
		if( entry.variableName.empty() || !entry.variable )
			continue;

		const std::string variableName = entry.variableName.c_str();
		const std::uint32_t rawValue = std::bit_cast<std::uint32_t>( entry.variable->i );

		auto [it, inserted] = previousValues.try_emplace( variableName, rawValue );

		if( inserted )
			continue;

		if( it->second == rawValue )
			continue;

		REX::DEBUG(
			"Graph variable changed: {} = 0x{:08X} -> 0x{:08X} | int: {} | float: {} | bool: {}",
			variableName,
			it->second,
			rawValue,
			entry.variable->i,
			entry.variable->f,
			entry.variable->b
		);

		it->second = rawValue;
	}
}

inline void SetAimHeadingCorrection( RE::Actor& a_actor, const RE::NiPoint3& a_origin, const RE::NiPoint3& a_target )
{
	static const RE::BSFixedString bowAimOffsetHeading{ "BowAimOffsetHeading" };

	const RE::NiPoint3 targetDirection = a_target - a_origin;
	const float targetHeading = atan2( targetDirection.x, targetDirection.y );
	const float relativeHeading = std::remainder( targetHeading - a_actor.GetAngleZ(), twoPi );

	constexpr float maximumHeadingTwist = 60.0f;

	const float headingOffset = std::clamp(
		-relativeHeading * radToDeg,
		-maximumHeadingTwist,
		maximumHeadingTwist
	);

	a_actor.SetGraphVariableFloat( bowAimOffsetHeading, headingOffset );
}

inline RE::CameraState GetCurrentPlayerCameraState()
{
	auto playerCamera = RE::PlayerCamera::GetSingleton();
	if( playerCamera && playerCamera->currentState )
		return playerCamera->currentState->id;

	return (RE::CameraState)-1;
}

inline float GetMagicEffectArea( const RE::MagicItem* a_magicItem )
{
	if( !a_magicItem )
		return 0.0f;

	auto avEffect = a_magicItem->GetAVEffect();
	if( !avEffect )
		return 0.0f;

	for( auto effect : a_magicItem->effects )
	{
		if( effect && effect->baseEffect == avEffect )
			return (float)effect->effectItem.area;
	}

	return 0.0f;
}

inline float GetProjectileImpactTolerance( const RE::BGSProjectile* a_projectile, const RE::MagicItem* a_spell )
{
	float tolerance = a_projectile ?
		a_projectile->data.collisionRadius :
		0.0f;

	if( a_spell )
		tolerance += GetMagicEffectArea( a_spell ) * feetToGameUnit;

	return tolerance;
}

#pragma warning(pop)
