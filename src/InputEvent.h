#pragma once
#include "Raycast.h"

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
			if( !button || !button->IsUp() )
				continue;

			auto userEvent = button->QUserEvent();

			if( userEvent == userEvents->rightAttack )
				Raycast::CaptureReleaseTarget( RE::MagicSystem::CastingSource::kRightHand );
			else if( userEvent == userEvents->leftAttack )
				Raycast::CaptureReleaseTarget( RE::MagicSystem::CastingSource::kLeftHand );
			else if( userEvent == userEvents->dualAttack )
					Raycast::CaptureReleaseTarget( RE::MagicSystem::CastingSource::kOther );
		}

		return RE::BSEventNotifyControl::kContinue;
	}

private:
	InputEventSink() = default;
};
