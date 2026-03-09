/// @file SpineEventHandler.hpp
/// @author dario
/// @date 12/01/2026.

#pragma once

#include "spine/AnimationState.h"

class SpineRendererSubNode;

class SpineEventHandler : public spine::AnimationStateListenerObject {
	SpineRendererSubNode* context;

public:
	SpineEventHandler(SpineRendererSubNode* ctx);

	void callback(spine::AnimationState* state, spine::EventType type, spine::TrackEntry* entry, spine::Event* event) override;
};
