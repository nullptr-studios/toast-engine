/**
 * @file application.hpp
 * @author Xein
 * @date 10 Feb 2026
 */

#pragma once
#include "export.hpp"

namespace toast {

class IApplication {
public:
	virtual ~IApplication() = default;

	virtual void begin() = 0;
	virtual void tick() = 0;
	virtual void destroy() = 0;
};

void TOAST_API pushApplicationLayer(IApplication* app);

void TOAST_API registerGameTypes();

}
