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

	// Registers the game's reflected types with the engine. Games override this
	// (typically forwarding to the generated registerGameTypes()); defaults to a no-op.
	// Called by pushApplicationLayer, so registration runs from inside the game module.
	virtual void registerTypes() { }
};

void TOAST_API pushApplicationLayer(IApplication* app);
void TOAST_API popApplicationLayer(IApplication* app);

}
