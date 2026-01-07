/// @file SceneLoadedEvent.hpp
/// @date 2/10/2025
/// @author Xein
/// @brief An event sent when the scene finishes being loaded

#pragma once
#include <Toast/Log.hpp>
#include <Toast/Event/Event.hpp>
#include <format>

namespace toast {

struct SceneLoadedEvent final : public event::Event<SceneLoadedEvent> {
	SceneLoadedEvent(const unsigned id, const std::string& name) : id(id), name(name) { }

	unsigned id;
	std::string name;
};

class BadScene : std::exception {
public:
	BadScene(unsigned id) : m_id(id) {
		m_error = std::format("Scene {0} couldn't be loaded correctly", m_id);
	}

	[[nodiscard]]
	const char* what() const noexcept override {
		TOAST_ERROR("{0}", m_error);
		return m_error.c_str();
	}

	[[nodiscard]]
	unsigned id() const noexcept {
		return m_id;
	}

private:
	unsigned m_id;
	std::string m_error {};
};

}
