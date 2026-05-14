/**
 * @file base_window.hpp
 * @author Xein
 * @date 7 May 2026
 *
 * @brief Interface for the Window class
 */

#pragma once
#include <cstdint>

namespace toast {

enum class WindowType : uint8_t {
	sdl = 0,
	avalonia = 1
};

class IBaseWindow {
public:
	IBaseWindow() = default;
	virtual ~IBaseWindow() = default;

	// Functions
	[[nodiscard]]
	virtual auto shouldClose() const -> bool = 0;
	virtual void pollEvents() = 0;
	virtual void swapFramebuffers() = 0;

	// Variables

protected:
	WindowType type;
};

}
