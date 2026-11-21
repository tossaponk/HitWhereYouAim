#include "Crosshair.h"
#include "Utils.h"
#include "Native.h"

struct SavedTransform
{
	RE::GRenderer::Cxform value;
	bool captured = false;
};

namespace
{
	float crosshairOriginalX = FLT_MAX;
	float crosshairOriginalY = FLT_MAX;
	uint32_t crosshairTintTimeout = UINT_MAX;
	uint32_t crosshairValidityTimeout = UINT_MAX;
	bool lastCrosshairValid = true;
	Crosshair::TintOwner currentTintOwner = Crosshair::TintOwner::kNone;
	std::mutex crosshairMutex;
	SavedTransform normalTransform;
	SavedTransform sneakTransform;

	static constexpr const char* normalPath = "_root.HUDMovieBaseInstance.CrosshairInstance";
	static constexpr const char* sneakPath = "_root.HUDMovieBaseInstance.StealthMeterInstance";
}

bool _CaptureOriginal( RE::GFxValue a_crosshair, Crosshair::Type a_type )
{
	auto& saved = a_type == Crosshair::Type::kNormal ? normalTransform : sneakTransform;

	if( saved.captured )
		return true;

	if( !a_crosshair.GetCxform( &saved.value ) )
		return false;

	saved.captured = true;
	return true;
}

bool Crosshair::SetPosition( float a_screenX, float a_screenY )
{
	// Touching UI Movie is dangerous without lock
	std::lock_guard lock{ crosshairMutex };

	auto* ui = RE::UI::GetSingleton();
	if( !ui )
		return false;

	auto movie = ui->GetMovieView( RE::HUDMenu::MENU_NAME );
	if( !movie )
		return false;

	RE::GViewport viewport;
	movie->GetViewport( &viewport );

	if( viewport.width <= 0 || viewport.height <= 0 )
		return false;

	auto frame = movie->GetVisibleFrameRect();
	float frameWidth = frame.right - frame.left;
	float frameHeight = frame.bottom - frame.top;

	float viewportCenterX = viewport.left + viewport.width * 0.5f;
	float viewportCenterY = viewport.top + viewport.height * 0.5f;

	float screenDeltaX = a_screenX - viewportCenterX;
	float screenDeltaY = a_screenY - viewportCenterY;

	constexpr float crosshairCenterX = 23.2f;
	constexpr float crosshairCenterY = -115.3f;

	float crosshairX = crosshairCenterX + screenDeltaX * frameWidth / viewport.width;
	float crosshairY = crosshairCenterY + screenDeltaY * frameHeight / viewport.height;

	RE::GFxValue crosshair;
	if( !movie->GetVariable( &crosshair, "_root.HUDMovieBaseInstance.CrosshairInstance" ) || !crosshair.IsDisplayObject() )
		return false;

	RE::GFxValue::DisplayInfo info;
	if( !crosshair.GetDisplayInfo( &info ) )
		return false;

	if( crosshairOriginalX == FLT_MAX )
	{
		crosshairOriginalX = viewportCenterX + (float)( info.GetX() - crosshairCenterX ) * viewport.width / frameWidth;
		crosshairOriginalY = viewportCenterY + (float)( info.GetY() - crosshairCenterY ) * viewport.height / frameHeight;
	}

	info.SetPosition( crosshairX, crosshairY );

	return crosshair.SetDisplayInfo( info );
}

void Crosshair::ResetPosition()
{
	if( crosshairOriginalX != FLT_MAX )
	{
		SetPosition( crosshairOriginalX, crosshairOriginalY );
		crosshairOriginalX = FLT_MAX;
		crosshairOriginalY = FLT_MAX;
	}
}

bool Crosshair::SetToWorldPosition( const RE::NiPoint3& a_world )
{
	auto* ui = RE::UI::GetSingleton();
	auto camera = RE::Main::WorldRootCamera();

	if( !ui || !camera )
		return false;

	float normalizedX;
	float normalizedY;
	float depth;

	if( !RE::NiCamera::WorldPtToScreenPt3(
		camera->GetRuntimeData().worldToCam,
		camera->GetRuntimeData2().port,
		a_world,
		normalizedX,
		normalizedY,
		depth,
		1.0e-5f ) )
	{
		return false;
	}

	auto movie = ui->GetMovieView( RE::HUDMenu::MENU_NAME );
	if( !movie )
		return false;

	RE::GViewport viewport;
	movie->GetViewport( &viewport );

	if( viewport.width <= 0 || viewport.height <= 0 )
		return false;

	float screenX = viewport.left + normalizedX * viewport.width;
	float screenY = viewport.top + ( 1.0f - normalizedY ) * viewport.height;

	return SetPosition( screenX, screenY );
}

bool Crosshair::GetCrosshair( RE::GFxValue& a_crosshair, Type a_type )
{
    auto ui = RE::UI::GetSingleton();
    auto hud = ui ? ui->GetMenu<RE::HUDMenu>() : nullptr;

    if( !hud || !hud->uiMovie )
        return false;

    const char* path = a_type == Type::kNormal ? normalPath : sneakPath;

    return hud->uiMovie->GetVariable( &a_crosshair, path ) && a_crosshair.IsDisplayObject();
}

bool Crosshair::ApplyTint( Type a_type, unsigned char a_red, unsigned char a_green, unsigned char a_blue, TintOwner a_owner, float a_timeoutSecond )
{
	RestoreTint( a_type, a_owner );

	std::lock_guard lock{ crosshairMutex };

	RE::GFxValue crosshair;

	if( currentTintOwner != TintOwner::kNone && currentTintOwner != a_owner )
		return false;

	if( !GetCrosshair( crosshair, a_type ) )
		return false;

	_CaptureOriginal( crosshair, a_type );

	auto& original = a_type == Type::kNormal ? normalTransform.value : sneakTransform.value;
	auto transform = original;

	const float red = (float)a_red / 255.0f;
	const float green = (float)a_green / 255.0f;
	const float blue = (float)a_blue / 255.0f;

	transform.matrix[ RE::GRenderer::Cxform::kR ][ RE::GRenderer::Cxform::kMult ] *= red;
	transform.matrix[ RE::GRenderer::Cxform::kG ][ RE::GRenderer::Cxform::kMult ] *= green;
	transform.matrix[ RE::GRenderer::Cxform::kB ][ RE::GRenderer::Cxform::kMult ] *= blue;

	if( !crosshair.SetCxform( transform ) )
		return false;

	currentTintOwner = a_owner;

	if( a_timeoutSecond != FLT_MAX )
		crosshairTintTimeout = (uint32_t)(GetCurrentGameRuntimeMS() + 1000 * a_timeoutSecond);
	else
		crosshairTintTimeout = UINT_MAX;

	return true;
}

bool Crosshair::ApplyTint( unsigned char a_red, unsigned char a_green, unsigned char a_blue, TintOwner a_owner, float a_timeoutSecond )
{
	const bool normalChanged = ApplyTint( Type::kNormal, a_red, a_green, a_blue, a_owner, a_timeoutSecond );
	const bool sneakChanged = ApplyTint( Type::kSneak, a_red, a_green, a_blue, a_owner, a_timeoutSecond );

	return normalChanged || sneakChanged;
}

bool Crosshair::ResetTint( TintOwner a_owner )
{
	const bool normalRestored =	RestoreTint( Type::kNormal, a_owner );
	const bool sneakRestored = RestoreTint( Type::kSneak, a_owner );

	currentTintOwner = TintOwner::kNone;
	crosshairTintTimeout = UINT_MAX;

	return normalRestored || sneakRestored;
}

bool Crosshair::RestoreTint( Type a_type, TintOwner a_owner )
{
	std::lock_guard lock{ crosshairMutex };

	auto& saved = a_type == Type::kNormal ? normalTransform : sneakTransform;

	if( currentTintOwner != a_owner )
		return false;

	if( !saved.captured )
		return true;

	RE::GFxValue crosshair;
	if( !GetCrosshair( crosshair, a_type ) )
		return false;

	if( !crosshair.SetCxform( saved.value ) )
		return false;

	saved.captured = false;
	return true;
}

void Crosshair::Invalidate()
{
	normalTransform.captured = false;
	sneakTransform.captured = false;
}

void Crosshair::Update()
{
#ifdef HWYA_PROFILER_ACTIVE
	Profiler::Scope profiler( Profiler::kCrosshairUpdate );
#endif // HWYA_PROFILER_ACTIVE

	auto time = GetCurrentGameRuntimeMS();
	if( time >= crosshairTintTimeout )
	{
		if( normalTransform.captured )
		{
			RestoreTint( Type::kNormal, currentTintOwner );
			normalTransform.captured = false;
		}

		if( sneakTransform.captured )
		{
			RestoreTint( Type::kSneak, currentTintOwner );
			sneakTransform.captured = false;
		}

		if( !normalTransform.captured && !sneakTransform.captured )
		{
			currentTintOwner = TintOwner::kNone;
			crosshairTintTimeout = UINT_MAX;
		}
	}

	if( time >= crosshairValidityTimeout )
	{
		Native::SetCrosshairValidity( true );
		crosshairValidityTimeout = UINT_MAX;

		if( currentTintOwner == TintOwner::kProjectilePrediction )
		{
			RestoreTint( Type::kSneak, TintOwner::kProjectilePrediction );

			if( !sneakTransform.captured )
				currentTintOwner = TintOwner::kNone;
		}
	}
}

void Crosshair::SetValidityState( bool a_valid )
{
	// Native forwarding controls the ordinary crosshair.
	Native::SetCrosshairValidity( a_valid );

	if( a_valid )
	{
		if( currentTintOwner == TintOwner::kProjectilePrediction )
		{
			RestoreTint( Type::kSneak, TintOwner::kProjectilePrediction );

			if( !normalTransform.captured && !sneakTransform.captured )
			{
				currentTintOwner = TintOwner::kNone;
				crosshairTintTimeout = UINT_MAX;
			}
		}

		crosshairValidityTimeout = UINT_MAX;
	}
	else
	{
		// Native validity forwarding does not recolor the sneak eye.
		ApplyTint( Type::kSneak, 255, 128, 64, TintOwner::kProjectilePrediction, 1.0f );

		crosshairValidityTimeout = GetCurrentGameRuntimeMS() + 1000;
	}
}
