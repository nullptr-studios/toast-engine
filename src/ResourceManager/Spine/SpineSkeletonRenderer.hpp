/// @file SpineSkeletonRenderer.hpp
/// @author dario
/// @date 25/10/2025.

#pragma once
#include "Engine/Core/Log.hpp"
#include "spine/SkeletonRenderer.h"

/// just expose it as a singelton
class SpineSkeletonRenderer : public spine::SkeletonRenderer {
public:
	SpineSkeletonRenderer() {
		TOAST_TRACE("Created SpineSkeletonRenderer");
	}

	static SpineSkeletonRenderer& getRenderer() {
		static SpineSkeletonRenderer m_renderer;
		return m_renderer;
	}
};
