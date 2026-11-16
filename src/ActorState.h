#pragma once
#include "Utils.h"

enum RangedAttackType
{
	kBow,
	kCrossbow,
	kLeftHand,
	kRightHand,
	kMaxSource,
	kNone = kMaxSource
};

inline RE::BGSProjectile* GetEquippedMagicProjectile( RE::Actor* a_actor, bool a_leftHand )
{
	if( !a_actor )
		return nullptr;

	auto equippedObject = a_actor->GetEquippedObject( a_leftHand );
	auto spell = equippedObject ? equippedObject->As<RE::SpellItem>() : nullptr;
	return GetMagicProjectile( spell );
}

inline RE::BGSProjectile* GetActorProjectileData( RE::Actor* a_actor, float* out_speed = nullptr, float* out_gravity = nullptr, bool* out_isLeftHand = nullptr )
{
	if( out_speed ) *out_speed = 0.0f;
	if( out_gravity ) *out_gravity = 0.0f;
	if( out_isLeftHand ) *out_isLeftHand = false;

	if( !a_actor )
		return nullptr;

	auto rightObject = a_actor->GetEquippedObject( false );
	auto leftObject = a_actor->GetEquippedObject( true );
	auto rightWeapon = rightObject ? rightObject->As<RE::TESObjectWEAP>() : nullptr;
	auto leftWeapon = leftObject ? leftObject->As<RE::TESObjectWEAP>() : nullptr;
	bool hasRangedWeapon = (rightWeapon && rightWeapon->IsRanged()) || (leftWeapon && leftWeapon->IsRanged());
	RE::BGSProjectile* projectile = nullptr;

	if( hasRangedWeapon )
	{
		auto ammo = a_actor->GetCurrentAmmo();
		if( ammo )
			projectile = ammo->data.projectile;
	}

	if( !projectile )
		projectile = GetEquippedMagicProjectile( a_actor, false );

	if( !projectile )
	{
		projectile = GetEquippedMagicProjectile( a_actor, true );
		if( projectile && out_isLeftHand ) *out_isLeftHand = true;
	}

	if( projectile )
	{
		bool isHitScan = projectile->IsBeam() || projectile->IsFlamethrower();
		if( out_speed ) *out_speed = isHitScan ? 1.e6f : projectile->data.speed;
		if( out_gravity ) *out_gravity = isHitScan ? 0 : projectile->data.gravity * Native::GetWorldGravity();
	}

	return projectile;
}

inline bool IsCastingRangedSpell( RE::Actor& a_actor, bool a_leftHand = false )
{
	auto equippedItem = a_actor.GetEquippedObject( a_leftHand );
	if( !equippedItem )
		return false;

	auto equippedSpell = equippedItem->As<RE::MagicItem>();
	if( !equippedSpell )
		return false;

	auto magicCaster = a_actor.GetMagicCaster(
		a_leftHand ?
		RE::MagicSystem::CastingSource::kLeftHand :
		RE::MagicSystem::CastingSource::kRightHand );

	return magicCaster && magicCaster->currentSpell == equippedSpell && a_actor.IsCasting( equippedSpell ) && IsRangedMagic( equippedSpell );
}

enum class AnimationState : int32_t
{
	kDefault				= 0,
	kSprinting				= 1,
	kSneaking				= 2,
	kBowDrawn				= 3,
	kBlocking				= 4,
	kBleedout				= 5,
	kOneHanded				= 6,
	kTwoHanded				= 7,
	kBow					= 8,
	kMagic					= 9,
	kMagicCasting			= 10,
	kHorse					= 11,
	kAttacking				= 12,
	kAttackingTwoHanded		= 13,
	kPowerAttacking			= 14,
	kDrunk					= 15,
	kBowDrawnQuickShot		= 16,
	kBlockingShieldCharge	= 17,
	kHorseDefault			= 60
};
inline bool IsUsingRangedAttack( RE::Actor& a_actor )
{
	int32_t state;
	if( a_actor.GetGraphVariableInt( "iState", state ) && state == (int32_t)AnimationState::kBowDrawn )
		return true;

	bool isAiming;
	if( a_actor.GetGraphVariableBool( "bAimActive", isAiming ) && isAiming )
		return true;

	return IsCastingRangedSpell( a_actor, false ) || IsCastingRangedSpell( a_actor, true );
}

inline RE::NiAVObject* GetFireNode( RE::Actor& a_actor )
{
	RE::NiAVObject* node = a_actor.GetFireNode();

	if( node )
		return node;

	auto leftHand = a_actor.GetMagicCaster( RE::MagicSystem::CastingSource::kLeftHand );
	if( leftHand && IsCastingRangedSpell( a_actor, true ) )
		return leftHand->GetMagicNode();

	auto rightHand = a_actor.GetMagicCaster( RE::MagicSystem::CastingSource::kRightHand );
	if( rightHand && IsCastingRangedSpell( a_actor, false ) )
		return rightHand->GetMagicNode();

	return a_actor.Get3D();
}

inline bool GetRangedAttackData(
	RE::Actor* a_actor,
	RangedAttackType* out_type = nullptr,
	RE::TESObjectWEAP** out_weapon = nullptr,
	RE::MagicItem** out_spell = nullptr,
	RE::BGSProjectile** out_projectileBase = nullptr,
	RE::MagicSystem::CastingSource* out_source = nullptr )
{
	if( !a_actor )
		return false;

	if( out_type )				*out_type = RangedAttackType::kNone;
	if( out_weapon )			*out_weapon = nullptr;
	if( out_spell )				*out_spell = nullptr;
	if( out_projectileBase )	*out_projectileBase = nullptr;
	if( out_source )			*out_source = RE::MagicSystem::CastingSource::kNone;

	auto equippedLeft = a_actor->GetEquippedObject( true );
	auto equippedRight = a_actor->GetEquippedObject( false );
	if( IsUsingRangedAttack( *a_actor ) )
	{
		// Ranged weapon is always in the right hand
		auto weapon = equippedRight ? equippedRight->As<RE::TESObjectWEAP>() : nullptr;
		if( out_weapon ) *out_weapon = weapon;

		if( weapon )
		{
			if( out_type ) *out_type = weapon->IsBow() ? RangedAttackType::kBow : RangedAttackType::kCrossbow;
			if( out_projectileBase )
			{
				auto ammo = a_actor->GetCurrentAmmo();
				*out_projectileBase = ammo ? ammo->data.projectile : nullptr;
			}

			return true;
		}
	}

	// Right-hand casting takes precedence over left-hand casting
	if( IsCastingRangedSpell( *a_actor, false ) )
	{
		auto spell = equippedRight ? equippedRight->As<RE::MagicItem>() : nullptr;
		if( out_spell ) *out_spell = spell;

		if( spell )
		{
			if( out_source ) *out_source = RE::MagicSystem::CastingSource::kRightHand;
			if( out_type ) *out_type = RangedAttackType::kRightHand;
			if( out_projectileBase ) *out_projectileBase = GetEquippedMagicProjectile( a_actor, false );
			return true;
		}	
	}

	// Left-hand casting
	if( IsCastingRangedSpell( *a_actor, true ) )
	{
		auto spell = equippedLeft ? equippedLeft->As<RE::MagicItem>() : nullptr;
		if( out_spell ) *out_spell = spell;

		if( spell )
		{
			if( out_source ) *out_source = RE::MagicSystem::CastingSource::kLeftHand;
			if( out_type ) *out_type = RangedAttackType::kLeftHand;
			if( out_projectileBase ) *out_projectileBase = GetEquippedMagicProjectile( a_actor, true );
			return true;
		}
	}

	return false;
}

struct BowDrawEntry
{
	float bowDrawTime;
	uint32_t unk04;
	uint64_t timestamp;
};
using BowDrawArray = RE::BSTSmallArray<BowDrawEntry, 2>;
static_assert( sizeof( BowDrawArray ) == sizeof( RE::BSTSmallArray<void*, 4> ) );

inline float GetCurrentBowDrawTimer( const RE::PlayerCharacter* a_player )
{
	if( !a_player )
		return 1.0f;

	auto& runtime = a_player->GetPlayerRuntimeData();
	const auto array = (BowDrawArray*)&runtime.unkBA0;

	if( array->size() <= 0 )
		return 1.0f;

	float drawTime = array->back().bowDrawTime;

	if( !isfinite( drawTime ) )
		return 1.0f;

	return drawTime;
}

inline float CalculateBowPower( float a_drawTime, float a_drawSpeed )
{
	static REL::Relocation<float*> fBowDrawStartTime{ RELOCATION_ID( 505062, 374220 ) }; // 505062, 374220
	static REL::Relocation<float*> fBowMaximumPower{ RELOCATION_ID( 228662, 186482 ) }; // 228662, 186482
	static REL::Relocation<float*> fBowFullDrawTime{ RELOCATION_ID( 505068, 374229 ) }; // 505068, 374229
	static REL::Relocation<float*> fBowMinimumPower{ RELOCATION_ID( 505070, 374232 ) }; // 505070, 374232

	float maximum = *fBowMaximumPower;
	float minimum = *fBowMinimumPower;
	float elapsedTime = a_drawTime - *fBowDrawStartTime;

	if( elapsedTime <= 0.0f )
		return minimum;

	if( a_drawSpeed <= 0.0f )
		a_drawSpeed = maximum;

	float drawDuration = ( *fBowFullDrawTime - *fBowDrawStartTime ) / a_drawSpeed;

	if( elapsedTime >= drawDuration )
		return maximum;

	return elapsedTime / drawDuration * ( maximum - minimum ) + minimum;
}
