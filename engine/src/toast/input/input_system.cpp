/**
 * @file input_system.cpp
 * @author Xein
 * @date 21 Jun 2026
 *
 * @brief Implementation of the input system singleton
 */

#include "input_system.hpp"

#include "input_events.hpp"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_sensor.h>
#include <chrono>
#include <toast/assets/assets.hpp>
#include <toast/events/event.hpp>
#include <toast/log.hpp>
#include <toast/window/window_events.hpp>

namespace input {

namespace {

constexpr float axis_normalize = 32767.0f;
constexpr float controller_activity_threshold = 0.5f;

auto toModifierKey(uint16_t sdl_mods) -> ModifierKey {
	ModifierKey mods = ModifierKey::none;
	if ((sdl_mods & SDL_KMOD_SHIFT) != 0) {
		mods = mods | ModifierKey::shift;
	}
	if ((sdl_mods & SDL_KMOD_CTRL) != 0) {
		mods = mods | ModifierKey::control;
	}
	if ((sdl_mods & SDL_KMOD_ALT) != 0) {
		mods = mods | ModifierKey::alt;
	}
	return mods;
}

auto gamepadName(SDL_Gamepad* pad) -> std::string {
	if (pad == nullptr) {
		return "controller";
	}
	switch (SDL_GetGamepadType(pad)) {
		case SDL_GAMEPAD_TYPE_XBOX360:
		case SDL_GAMEPAD_TYPE_XBOXONE: return "controller, xbox";
		case SDL_GAMEPAD_TYPE_PS3:
		case SDL_GAMEPAD_TYPE_PS4:
		case SDL_GAMEPAD_TYPE_PS5: return "controller, playstation";
		case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
		case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
		case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
		case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return "controller, nintendo";
		default: return "controller";
	}
}

}

InputSystem::InputSystem() {
	instance = this;

	if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
		TOAST_WARN("Input", "Could not initialize the SDL gamepad subsystem: {}", SDL_GetError());
	}

	subscribeEvents();
	reloadActions();

	TOAST_INFO("Input", "Input system created");
}

InputSystem::~InputSystem() {
	for (auto& [_, pad] : m_gamepads) {
		if (pad != nullptr) {
			SDL_CloseGamepad(pad);
		}
	}
	m_gamepads.clear();
	if (instance == this) {
		instance = nullptr;
	}
}

auto InputSystem::get() noexcept -> InputSystem& {
	return *instance;
}

void InputSystem::subscribeEvents() {
	m_listener.subscribe<event::WindowResize>([this](const event::WindowResize& e) {
		// TODO: The cursor NDC conversion needs the active viewport; track the full window by default
		if (e.width > 0 && e.height > 0) {
			setViewport(glm::vec2 {0.0f}, glm::vec2 {static_cast<float>(e.width), static_cast<float>(e.height)});
		}
		return false;
	});

	m_listener.subscribe<event::ReloadInputActions>([this](const event::ReloadInputActions&) {
		reloadActions();
		return false;
	});

	m_listener.subscribe<event::WindowKey>([this](const event::WindowKey& e) {
		m_mods = toModifierKey(static_cast<uint16_t>(e.mods));
		if (e.action == event::window_input_released) {
			m_keys_down.erase(e.key);
		} else {
			m_keys_down.insert(e.key);
			noteLastInput(Device::keyboard, "keyboard");
		}
		return false;
	});

	m_listener.subscribe<event::WindowMouseButton>([this](const event::WindowMouseButton& e) {
		m_mods = toModifierKey(static_cast<uint16_t>(e.mods));
		if (e.action == event::window_input_released) {
			m_mouse_down.erase(e.button);
		} else {
			m_mouse_down.insert(e.button);
			noteLastInput(Device::mouse, "mouse");
		}
		return false;
	});

	m_listener.subscribe<event::WindowMousePosition>([this](const event::WindowMousePosition& e) {
		m_mouse_position = glm::vec2 {e.x, e.y};
		return false;
	});

	m_listener.subscribe<event::WindowMouseScroll>([this](const event::WindowMouseScroll& e) {
		m_scroll += glm::vec2 {e.x, e.y};
		noteLastInput(Device::mouse, "mouse");
		return false;
	});
}

void InputSystem::reloadActions() {
	m_actions.clear();
	const auto uids = assets::listByType("input_action");
	for (toast::UID uid : uids) {
		auto handle = assets::load<assets::Action>(uid);
		if (auto action = Action::fromAsset(handle)) {
			m_actions.push_back(std::move(action));
		} else {
			TOAST_WARN("Input", "Failed to build action from asset {}", uid);
		}
	}
	TOAST_INFO("Input", "Loaded {} of {} input action(s)", m_actions.size(), uids.size());
}

void InputSystem::refreshGamepads() {
	// Reconcile the set of open gamepads with what SDL currently reports
	int count = 0;
	SDL_JoystickID* ids = SDL_GetGamepads(&count);

	std::unordered_map<uint32_t, SDL_Gamepad*> next;
	if (ids != nullptr) {
		for (int i = 0; i < count; ++i) {
			const uint32_t id = ids[i];
			if (auto it = m_gamepads.find(id); it != m_gamepads.end()) {
				next[id] = it->second;
				m_gamepads.erase(it);
			} else if (SDL_Gamepad* pad = SDL_OpenGamepad(id)) {
				// Enable the gyro sensor so the controller/gyro_* keycodes can be sampled
				if (SDL_GamepadHasSensor(pad, SDL_SENSOR_GYRO)) {
					SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_GYRO, true);
				}
				next[id] = pad;
				TOAST_INFO("Input", "Gamepad connected: {}", gamepadName(pad));
			}
		}
		SDL_free(ids);
	}

	// Anything left in the old map was disconnected
	for (auto& [_, pad] : m_gamepads) {
		if (pad != nullptr) {
			TOAST_INFO("Input", "Gamepad disconnected: {}", gamepadName(pad));
			SDL_CloseGamepad(pad);
		}
	}
	m_gamepads = std::move(next);

	// Detect controller activity so the last-input device follows the controller
	if (SDL_Gamepad* pad = activeGamepad()) {
		bool active = false;
		for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT && !active; ++b) {
			active = SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(b));
		}
		for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT && !active; ++a) {
			const float v = static_cast<float>(SDL_GetGamepadAxis(pad, static_cast<SDL_GamepadAxis>(a))) / axis_normalize;
			active = std::abs(v) >= controller_activity_threshold;
		}
		if (active) {
			noteLastInput(Device::controller, gamepadName(pad));
		}
	}
}

void InputSystem::noteLastInput(Device device, std::string name) {
	if (m_last_device == device && m_last_name == name) {
		return;
	}
	m_last_device = device;
	m_last_name = std::move(name);
	event::send<event::LastInputType>(m_last_device, m_last_name);
}

auto InputSystem::activeGamepad() const noexcept -> SDL_Gamepad* {
	if (m_gamepads.empty()) {
		return nullptr;
	}
	return m_gamepads.begin()->second;
}

auto InputSystem::activeGamepadId() const noexcept -> uint32_t {
	if (m_gamepads.empty()) {
		return 0;
	}
	return m_gamepads.begin()->first;
}

auto InputSystem::sample(const KeyCode& key) -> InputSample {
	InputSample out;
	if (!key.valid) {
		out.present = false;
		return out;
	}

	switch (key.device) {
		case Device::keyboard: out.scalar = m_keys_down.contains(key.code) ? 1.0f : 0.0f; break;

		case Device::mouse:
			switch (key.kind) {
				case InputKind::button: out.scalar = m_mouse_down.contains(key.code) ? 1.0f : 0.0f; break;
				case InputKind::scroll: out.scalar = key.code == 1 ? m_scroll.x : m_scroll.y; break;
				case InputKind::cursor:
					out.is_vector = true;
					out.vector = m_mouse_position;
					break;
				default: break;
			}
			break;

		case Device::controller: {
			SDL_Gamepad* pad = activeGamepad();
			out.present = pad != nullptr;
			if (pad == nullptr) {
				break;
			}
			switch (key.kind) {
				case InputKind::button:
					out.scalar = SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(key.code)) ? 1.0f : 0.0f;
					break;
				case InputKind::axis1d:
					if (key.code >= controller_gyro_x) {
						std::array<float, 3> gyro {0.0f, 0.0f, 0.0f};
						SDL_GetGamepadSensorData(pad, SDL_SENSOR_GYRO, gyro.data(), static_cast<int>(gyro.size()));
						out.scalar = gyro[static_cast<size_t>(key.code - controller_gyro_x)];
					} else {
						out.scalar = static_cast<float>(SDL_GetGamepadAxis(pad, static_cast<SDL_GamepadAxis>(key.code))) / axis_normalize;
					}
					break;
				case InputKind::axis2d:
					out.is_vector = true;
					if (key.code == controller_stick_left) {
						out.vector = {
						  static_cast<float>(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX)) / axis_normalize,
						  static_cast<float>(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY)) / axis_normalize,
						};
					} else if (key.code == controller_stick_right) {
						out.vector = {
						  static_cast<float>(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX)) / axis_normalize,
						  static_cast<float>(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY)) / axis_normalize,
						};
					} else if (key.code == controller_touchpad && SDL_GetNumGamepadTouchpads(pad) > 0) {
						bool down = false;
						float x = 0.0f;
						float y = 0.0f;
						SDL_GetGamepadTouchpadFinger(pad, 0, 0, &down, &x, &y, nullptr);
						out.vector = down ? glm::vec2 {x, y} : glm::vec2 {0.0f};
					} else {
						out.vector = glm::vec2 {0.0f};
					}
					break;
				default: break;
			}
			break;
		}

		default: out.present = false; break;
	}

	return out;
}

void InputSystem::tick() {
	const auto now = std::chrono::steady_clock::now();
	float delta = 0.0f;
	if (m_has_last_tick) {
		delta = std::chrono::duration<float>(now - m_last_tick).count();
	}
	m_last_tick = now;
	m_has_last_tick = true;

	m_mouse_delta = m_mouse_position - m_prev_mouse_position;

	refreshGamepads();

	EvalContext ctx;
	ctx.delta = delta;
	ctx.mods = m_mods;
	ctx.viewport_pos = m_viewport_position;
	ctx.viewport_size = m_viewport_size;

	auto sampler = [this](const KeyCode& key) { return sample(key); };

	for (auto& action : m_actions) {
		const SignalList signals = action->evaluate(sampler, ctx);
		for (ActionEvent event_type : signals) {
			event::send<event::InputEvent>(action->uid(), *action, event_type);
		}
	}

	// Reset per-frame transient input now that this frame has consumed it
	m_scroll = glm::vec2 {0.0f};
	m_prev_mouse_position = m_mouse_position;
}

void InputSystem::setViewport(glm::vec2 position, glm::vec2 size) noexcept {
	m_viewport_position = position;
	m_viewport_size = size;
}

auto InputSystem::lastInputDevice() const noexcept -> Device {
	return m_last_device;
}

auto InputSystem::lastInputName() const noexcept -> std::string_view {
	return m_last_name;
}

auto InputSystem::mousePosition() const noexcept -> glm::vec2 {
	return m_mouse_position;
}

auto InputSystem::mouseDelta() const noexcept -> glm::vec2 {
	return m_mouse_delta;
}

}
