/// @file SpineEventHandler.hpp
/// @author dario
/// @date 12/01/2026.

#pragma once

#include "spine/AnimationState.h"

class SpineRendererComponent;

class SpineEventHandler : public spine::AnimationStateListenerObject {
	SpineRendererComponent* context;

public:
	SpineEventHandler(SpineRendererComponent* ctx);

	void callback(spine::AnimationState* state, spine::EventType type, spine::TrackEntry* entry, spine::Event* event) override;
};
