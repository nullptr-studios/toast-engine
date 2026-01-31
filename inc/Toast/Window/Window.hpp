/**
 * @file Window.hpp
 * @author Xein
 * @date 03/05/25
 * @brief Window management for the Toast Engine.
 *
 * This file provides the Window class which manages the application window,
 * OpenGL context, and OS event handling through GLFW.
 */

#pragma once
#include "Toast/Event/Event.hpp"
#include "glm/vec2.hpp"

struct GLFWwindow;

namespace toast {

/**
 * @struct WindowProps
 * @brief Properties describing a window's configuration.
 */
struct WindowProps {
	unsigned width = -1;        ///< Window width in pixels.
	unsigned height = -1;       ///< Window height in pixels.
	std::string name = "-1";    ///< Window title.
};

enum class DisplayMode : uint8_t {
	WINDOWED = 0,
	FULLSCREEN = 1
};

/**
 * @class Window
 * @brief Singleton class managing the application window.
 *
 * The Window class wraps GLFW to provide window creation, event handling,
 * and OpenGL context management. It follows the singleton pattern as only
 * one window is supported.
 *
 * @par Features:
 * - Window creation and destruction
 * - Double-buffered rendering with buffer swap
 * - OS event polling (keyboard, mouse, window events)
 * - Framebuffer size queries
 * - Display scaling for HiDPI support
 *
 * @par Usage Example:
 * @code
 * // Window is created automatically by Engine
 * auto* window = Window::GetInstance();
 *
 * // Check framebuffer size
 * auto [width, height] = window->GetFramebufferSize();
 *
 * // Check if minimized
 * if (window->IsMinimized()) {
 *     // Skip rendering
 * }
 * @endcode
 *
 * @note The window is created and managed by the Engine class.
 * @warning Only one Window instance can exist at a time.
 *
 * @see Engine, WindowException
 */
class Window {
public:
	/**
	 * @brief Creates a window with the specified dimensions.
	 * @param width Initial window width in pixels.
	 * @param height Initial window height in pixels.
	 * @param name Window title.
	 * @throws ToastException if a window already exists.
	 */
	Window(unsigned width = 800, unsigned height = 600, const std::string& name = "Toast Engine");

	/**
	 * @brief Destroys the window and terminates GLFW.
	 */
	~Window();

	Window(const Window&) = delete;
	Window& operator=(const Window&) = delete;

	/**
	 * @brief Gets the singleton window instance.
	 * @return Pointer to the window, or nullptr if not created.
	 */
	static Window* GetInstance() {
		return m_instance;
	}

	/**
	 * @brief Swaps the front and back buffers.
	 *
	 * Call this at the end of each frame after rendering to display
	 * the rendered content.
	 */
	void SwapBuffers();

	/**
	 * @brief Checks if the window should close.
	 * @return true if close was requested (X button, Alt+F4, etc.).
	 */
	[[nodiscard]]
	bool ShouldClose() const;

	/**
	 * @brief Gets the framebuffer dimensions.
	 *
	 * The framebuffer size may differ from the window size on HiDPI displays.
	 *
	 * @return Pair of (width, height) in pixels.
	 */
	[[nodiscard]]
	glm::uvec2 GetFramebufferSize() const;

	/**
	 * @brief Gets the display scale factors.
	 *
	 * Returns the content scale for HiDPI displays (e.g., 2.0 for Retina).
	 *
	 * @return Pair of (scaleX, scaleY) factors.
	 */
	[[nodiscard]]
	std::pair<float, float> GetDisplayScale() const;

	/**
	 * @brief Gets time since window creation.
	 * @return Time in seconds since the window was created.
	 */
	double GetTime();

	/**
	 * @brief Gets the clipboard contents.
	 * @return Text currently in the system clipboard.
	 */
	std::string GetClipboard();

	/**
	 * @brief Checks if the window is minimized.
	 *
	 * Use this to skip rendering when the window is not visible.
	 *
	 * @return true if the window is minimized or has zero size.
	 */
	bool IsMinimized() const;

	/**
	 * @brief Polls OS events without swapping buffers.
	 *
	 * Processes pending window events (input, resize, etc.).
	 * Called automatically by the engine.
	 */
	void PollEventsOnly();

	/**
	 * @brief Waits for events with a timeout.
	 *
	 * Blocks until an event occurs or the timeout expires.
	 * Useful for reducing CPU usage in idle applications.
	 *
	 * @param seconds Maximum time to wait in seconds.
	 */
	void WaitEventsTimeout(double seconds);
	
	void SetDisplayMode(DisplayMode mode);
	DisplayMode GetDisplayMode() const;
	
	void SetResolution(glm::uvec2 res) const;

	/**
	 * @brief Gets the underlying GLFW window handle.
	 * @return Raw GLFW window pointer.
	 */
	[[nodiscard]]
	GLFWwindow* GetWindow() const {
		return m_glfwWindow;
	}

private:
	/// @brief Singleton instance pointer.
	static Window* m_instance;

	/// @brief Raw GLFW window pointer.
	GLFWwindow* m_glfwWindow;

	/// @brief Window configuration properties.
	WindowProps m_properties;

	/// @brief Event listener for window events.
	event::ListenerComponent m_listener;
	
	toast::DisplayMode m_currentDisplayMode = toast::DisplayMode::WINDOWED;
	
	glm::uvec2 m_windowedSize {};
	glm::ivec2 m_windowedPos {};

	/**
	 * @brief GLFW error callback handler.
	 * @param error GLFW error code.
	 * @param description Error description string.
	 */
	static void ErrorCallback(int error, const char* description);
};

/**
 * @class WindowException
 * @brief Exception thrown when a GLFW error occurs.
 *
 * This exception wraps GLFW error codes and descriptions for
 * proper error handling and reporting.
 */
class WindowException : public std::exception {
public:
	/**
	 * @brief Constructs a window exception.
	 * @param error GLFW error code.
	 * @param description Error description from GLFW.
	 */
	WindowException(int error, const char* description);

	int error;                  ///< GLFW error code.
	const char* description;    ///< Error description from GLFW.
	std::string message;        ///< Formatted error message.

	/**
	 * @brief Gets the error message.
	 * @return Formatted error string.
	 */
	[[nodiscard]]
	const char* what() const noexcept override;
};

}
