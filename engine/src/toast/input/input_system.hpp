/**
 * @file input_system.hpp
 * @author Xein
 * @date 21 Jun 2026
 *
 * @brief Owns all runtime input state and dispatches input events
 */

#pragma once

#include "action.hpp"
#include "keycodes.hpp"
#include "value.hpp"

#include <glm/glm.hpp>
#include <toast/events/listener.hpp>
#include <unordered_map>
#include <unordered_set>

struct SDL_Gamepad;

namespace input {

class TOAST_API InputSystem {
public:
	InputSystem();
	~InputSystem();

	InputSystem(const InputSystem&) = delete;
	auto operator=(const InputSystem&) -> InputSystem& = delete;

	[[nodiscard]]
	static auto get() noexcept -> InputSystem&;

	void tick();
	void reloadActions();
	void setViewport(glm::vec2 position, glm::vec2 size) noexcept;

	[[nodiscard]]
	auto lastInputDevice() const noexcept -> Device;            ///< @returns The most recently used input device

	[[nodiscard]]
	auto lastInputName() const noexcept -> std::string_view;    ///< @returns The most recently used input device

	[[nodiscard]]
	auto mousePosition() const noexcept -> glm::vec2;           ///< @returns The latest mouse cursor position in screen pixels

	[[nodiscard]]
	auto mouseDelta() const noexcept -> glm::vec2;              ///< @returns The delta mouse movement

	[[nodiscard]]
	auto activeGamepadId() const noexcept -> uint32_t;    ///< @returns SDL_JoystickID of the first connected gamepad, or 0 if none

private:
	void subscribeEvents();
	void refreshGamepads();
	void noteLastInput(Device device, std::string name);

	[[nodiscard]]
	auto activeGamepad() const noexcept -> SDL_Gamepad*;
	[[nodiscard]]
	auto sample(const KeyCode& key) -> InputSample;

	static inline InputSystem* instance = nullptr;

	event::Listener m_listener;
	std::vector<std::unique_ptr<Action>> m_actions;

	std::unordered_set<int> m_keys_down;
	std::unordered_set<int> m_mouse_down;
	ModifierKey m_mods = ModifierKey::none;

	glm::vec2 m_mouse_position {0.0f};
	glm::vec2 m_prev_mouse_position {0.0f};
	glm::vec2 m_mouse_delta {0.0f};
	glm::vec2 m_scroll {0.0f};

	glm::vec2 m_viewport_position {0.0f};
	glm::vec2 m_viewport_size {0.0f};

	std::unordered_map<uint32_t, SDL_Gamepad*> m_gamepads;

	Device m_last_device = Device::none;
	std::string m_last_name;

	std::chrono::steady_clock::time_point m_last_tick;
	bool m_has_last_tick = false;
};

}
