#include <Engine/Core/Log.hpp>
#include <Engine/Core/Profiler.hpp>
#include <Engine/Window/Window.hpp>
#include <Engine/Window/WindowEvents.hpp>

// clang-format off
#include <glad/glad.h>
#include <GLFW/glfw3.h>
// clang-format on

namespace toast {

Window* Window::m_instance = nullptr;

Window::Window(unsigned width, unsigned height, const std::string& name) {
	// Set window instance
	if (m_instance != nullptr) {
		throw ToastException("Trying to create window but it already exists");
	}
	m_instance = this;

	TOAST_ASSERT(glfwInit(), "Couldn't initialize GLFW");
	glfwSetErrorCallback(ErrorCallback);

	m_properties.width = width;
	m_properties.height = height;
	m_properties.name = name;

	TOAST_INFO("Creating window {0} ({1}, {2})", m_properties.name, m_properties.width, m_properties.height);

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	m_glfwWindow = glfwCreateWindow(width, height, name.c_str(), nullptr, nullptr);

	glfwMakeContextCurrent(m_glfwWindow);
	glfwSwapInterval(0);

	// Bind the close event to actually close the window
	m_listener.Subscribe<event::WindowClose>([&](event::WindowClose* e) -> bool {
		glfwSetWindowShouldClose(m_glfwWindow, true);
		return true;
	});

	// Set glfw to listen to the callbacks
	glfwSetKeyCallback(m_glfwWindow, event::WindowKey::Callback);
	glfwSetCharCallback(m_glfwWindow, event::WindowChar::Callback);
	glfwSetCursorPosCallback(m_glfwWindow, event::WindowMousePosition::Callback);
	glfwSetMouseButtonCallback(m_glfwWindow, event::WindowMouseButton::Callback);
	glfwSetScrollCallback(m_glfwWindow, event::WindowMouseScroll::Callback);
	glfwSetJoystickCallback(event::WindowInputDevice::Callback);
	glfwSetDropCallback(m_glfwWindow, event::WindowDrop::Callback);
	glfwSetFramebufferSizeCallback(m_glfwWindow, event::WindowResize::Callback);
}

Window::~Window() {
	TOAST_INFO("Destroying window");
	glfwDestroyWindow(m_glfwWindow);
	glfwTerminate();
}

bool Window::ShouldClose() const {
	return glfwWindowShouldClose(m_glfwWindow);
}

std::pair<unsigned, unsigned> Window::GetFramebufferSize() const {
	int w = 0, h = 0;
	glfwGetFramebufferSize(m_glfwWindow, &w, &h);
	return { static_cast<unsigned>(w), static_cast<unsigned>(h) };
}

std::pair<float, float> Window::GetDisplayScale() const {
	float wx = 0, wy = 0;
	glfwGetWindowContentScale(m_glfwWindow, &wx, &wy);

	return { wx, wy };
}

double Window::GetTime() {
	return glfwGetTime();
}

void Window::SwapBuffers() {
	PROFILE_ZONE;
	glfwSwapBuffers(m_glfwWindow);
}

void Window::PollEventsOnly() {
	glfwPollEvents();
}

void Window::WaitEventsTimeout(double seconds) {
	glfwWaitEventsTimeout(seconds);
}

bool Window::IsMinimized() const {
	auto [w, h] = GetFramebufferSize();
	return w == 0 || h == 0;
}

#pragma region Error_Handling

void Window::ErrorCallback(int error, const char* description) {
	throw WindowException(error, description);
}

WindowException::WindowException(int error, const char* description) : error(error), description(description) {
	std::ostringstream oss;
	oss << error << ": " << description;
	message = oss.str();
	TOAST_ERROR("GLFW Error {0}: {1}", error, description);
}

const char* WindowException::what() const noexcept {
	return message.c_str();
}

#pragma endregion

}
