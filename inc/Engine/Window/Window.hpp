/// @file Window.hpp
/// @author Xein
/// @date 03/05/25
/// @brief Contains the main window class, the window exceptions and the window properties

// TODO: Handle vsync

#pragma once
#include <Engine/Event/Event.hpp>

struct GLFWwindow;

namespace toast {

/// @brief Holds all window properties
struct WindowProps {
	unsigned width = -1;
	unsigned height = -1;
	std::string name = "-1";

	// bool vsync = false;
};

/// @brief Main window class of the engine
class Window {
public:
	/// @brief Creates a window with a given size
	Window(unsigned width = 800, unsigned height = 600, const std::string& name = "Toast Engine");
	~Window();

	Window(const Window&) = delete;
	Window& operator=(const Window&) = delete;

	static Window* GetInstance() {
		return m_instance;
	}

	/// @brief Swaps the window buffers only (no event polling)
	void SwapBuffers();

	[[nodiscard]]
	/// @return @c true if the engine is asking for the window to close
	bool ShouldClose() const;

	[[nodiscard]]
	/// @brief Returns the width and height of the framebuffer
	std::pair<unsigned, unsigned> GetFramebufferSize() const;

	[[nodiscard]]
	/// @brief Returns the user window scaling
	std::pair<float, float> GetDisplayScale() const;

	/// @brief Gets the time passed since the window was created
	double GetTime();

	/// @brief Returns the text stored on the user's clipboard
	std::string GetClipboard();

	/// @brief Returns true if the window is minimized/iconified or has a zero-sized framebuffer
	/// @note This can be used to skip heavy per-frame work when the window is not visible
	bool IsMinimized() const;

	/// @brief Poll OS events without swapping buffers
	void PollEventsOnly();

	/// @brief Block and wait for OS events for up to the given timeout in seconds
	void WaitEventsTimeout(double seconds);

	[[nodiscard]]
	GLFWwindow* GetWindow() const {
		return m_glfwWindow;
	}

private:
	static Window* m_instance;

	GLFWwindow* m_glfwWindow;    /// @brief Raw GLFW window pointer
	WindowProps m_properties;    /// @brief Struct containing all the window properties

	event::ListenerComponent m_listener;

	static void ErrorCallback(int error, const char* description);
};

/// @brief Exception to handle all GLFW error callbacks
class WindowException : public std::exception {
public:
	/// @param error GLFW error code
	/// @param description Description of the error
	WindowException(int error, const char* description);

	int error;
	const char* description;
	std::string message;

	[[nodiscard]]
	const char* what() const noexcept override;
};

}
