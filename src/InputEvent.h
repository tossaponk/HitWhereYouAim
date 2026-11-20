#pragma once
#include "Raycast.h"
#include "PredictorTelemetry.h"

class InputEventSink final :
	public RE::BSTEventSink<RE::InputEvent*>
{
public:
	static InputEventSink* GetSingleton()
	{
		static InputEventSink singleton;
		return &singleton;
	}

	RE::BSEventNotifyControl ProcessEvent(
		RE::InputEvent* const* a_events,
		RE::BSTEventSource<RE::InputEvent*>* ) override
	{
		if( !a_events )
			return RE::BSEventNotifyControl::kContinue;

		auto userEvents = RE::UserEvents::GetSingleton();
		if( !userEvents )
			return RE::BSEventNotifyControl::kContinue;

		for( auto event = *a_events; event; event = event->next )
		{
			auto button = event->AsButtonEvent();

			if( !button )
				continue;

#ifdef _DEBUG
			if( button->IsUp() && button->idCode == 0x57 )
				ProjectilePrediction::PredictorTelemetry::GetSingleton()->Report();
#endif

			auto userEvent = button->QUserEvent();

			if( userEvent == userEvents->readyWeapon )
			{
				if( button->IsDown() )
				{
					_armedReleaseSources = 0;
					Raycast::ClearReleaseTargetCapture();
					TrueHUDArrowTrail::Stop( TrueHUDArrowTrail::Type::kPredictorTrail );
				}

				continue;
			}

			uint8_t sourceFlag = 0;
			RE::MagicSystem::CastingSource castingSource = RE::MagicSystem::CastingSource::kOther;

			if( userEvent == userEvents->rightAttack )
			{
				sourceFlag = kRightHand;
				castingSource = RE::MagicSystem::CastingSource::kRightHand;
			}
			else if( userEvent == userEvents->leftAttack )
			{
				sourceFlag = kLeftHand;
				castingSource = RE::MagicSystem::CastingSource::kLeftHand;
			}
			else if( userEvent == userEvents->dualAttack )
			{
				sourceFlag = kDualCast;
				castingSource = RE::MagicSystem::CastingSource::kOther;
			}
			else
			{
				continue;
			}

			if( button->IsDown() )
			{
				_armedReleaseSources |= sourceFlag;
			}
			else if( button->IsUp() )
			{
				if(( _armedReleaseSources & sourceFlag ) != 0 )
					Raycast::CaptureReleaseTarget( castingSource );

				_armedReleaseSources &= ~sourceFlag;
			}
		}

		return RE::BSEventNotifyControl::kContinue;
	}

private:
	static constexpr uint8_t kRightHand = 1 << 0;
	static constexpr uint8_t kLeftHand = 1 << 1;
	static constexpr uint8_t kDualCast = 1 << 2;

	InputEventSink() = default;

	uint8_t _armedReleaseSources = 0;
};
