#pragma once

namespace Crosshair
{
	enum class Type
	{
		kNormal,
		kSneak
	};

	enum class TintOwner
	{
		kNone,
		kProjectilePrediction,
		kTargetCorrection,
		kOther
	};

	bool SetPosition( float a_screenX, float a_screenY );
	void ResetPosition();
	bool SetToWorldPosition( const RE::NiPoint3& a_world );
	bool GetCrosshair( RE::GFxValue& a_crosshair, Type a_type );
	bool ApplyTint( Type a_type, unsigned char a_red, unsigned char a_green, unsigned char a_blue, TintOwner a_owner, float a_timeoutSecond = FLT_MAX );
	bool ApplyTint( unsigned char a_red, unsigned char a_green, unsigned char a_blue, TintOwner a_owner, float a_timeoutSecond = FLT_MAX );
	bool ResetTint( TintOwner a_owner );
	bool RestoreTint( Type a_type, TintOwner a_owner );
	void Invalidate();
	void Update();

	void SetValidityState( bool a_valid );
}

