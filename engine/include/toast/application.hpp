/// @file application.hpp
/// @author Xein
/// @date 10 Feb 2026

#pragma once
#include "engine.hpp"

namespace toast {

class IApplication {
	virtual void Begin() = 0;
	virtual void Tick() = 0;
	virtual void Destroy() = 0;
};

void pushApplicationLayer(IApplication* app);

}
