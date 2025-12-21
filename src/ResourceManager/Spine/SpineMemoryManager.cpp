/// @file SpineMemoryManager.cpp
/// @author dario
/// @date 25/10/2025.

// include it just to make it compile, yes its ass
#include "SpineMemoryManager.hpp"

namespace spine {

SpineExtension* getDefaultExtension() {
	static EngineSpineExtension instance;
	return &instance;
}

}    // namespace spine
