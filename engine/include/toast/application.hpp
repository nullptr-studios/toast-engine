/**
 * @file application.hpp
 * @author Xein
 * @date 10 Feb 2026
 */

#pragma once
#include "export.hpp"

namespace toast {

class TOAST_API IApplication {
	virtual void Begin() = 0;
	virtual void Tick() = 0;
	virtual void Destroy() = 0;
};

void TOAST_API pushApplicationLayer(IApplication* app);

}
