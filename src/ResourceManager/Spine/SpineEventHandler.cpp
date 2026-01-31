/// @file SpineEventHandler.cpp
/// @author dario
/// @date 12/01/2026.

#include "Toast/Resources/Spine/SpineEventHandler.hpp"

#include "Toast/Components/SpineRendererComponent.hpp"
#include "spine/Animation.h"
#include "spine/Event.h"
#include "spine/EventData.h"

SpineEventHandler::SpineEventHandler(SpineRendererComponent* ctx) : context(ctx) { }

void SpineEventHandler::callback(spine::AnimationState* state, spine::EventType type, spine::TrackEntry* entry, spine::Event* event) {
	switch (type) {
		case spine::EventType_Start:
			context->OnAnimationStart(entry->getAnimation()->getName().buffer(), entry->getTrackIndex());
			TOAST_TRACE("Spine: Animation started: {}", entry->getAnimation()->getName().buffer());
			break;
		case spine::EventType_Interrupt:
			context->OnAnimationInterrupted(entry->getAnimation()->getName().buffer(), entry->getTrackIndex());
			TOAST_TRACE("Spine: Animation interrupted");
			break;
		case spine::EventType_End:
			context->OnAnimationEnd(entry->getAnimation()->getName().buffer(), entry->getTrackIndex());
			TOAST_TRACE("Spine: Animation ended");
			break;
		case spine::EventType_Complete:
			context->OnAnimationCompleted(entry->getAnimation()->getName().buffer(), entry->getTrackIndex());
			TOAST_TRACE("Spine: Animation completed (loops fire this each loop)");
			break;
		case spine::EventType_Dispose:
			context->OnAnimationDispose(entry->getAnimation()->getName().buffer(), entry->getTrackIndex());
			TOAST_TRACE("Spine: Track entry disposed");
			break;
		case spine::EventType_Event:
			// User-defined event from animation
			if (event) {
				std::string name = event->getData().getName().buffer();
				TOAST_TRACE("Spine Event: {}", name.c_str());

				// Access event data
				int intValue = event->getIntValue();
				float floatValue = event->getFloatValue();
				const std::string& stringValue = event->getStringValue().buffer();

				context->OnAnimationEvent(entry->getAnimation()->getName().buffer(), entry->getTrackIndex(), name, intValue, floatValue, stringValue);

				if (name == "PlayFx") {
					TOAST_TRACE("Spine PlayFx: {}", stringValue);
					//@TODO
					// context->PlayFxEvent(intValue, floatValue, stringValue);
				} else if (name == "PlaySound") {
					TOAST_TRACE("Spine PlaySound: {}", stringValue);
					//@TODO
					// context->SpawnParticleEvent(intValue, floatValue, stringValue);
				}
			}
			break;
	}
}
