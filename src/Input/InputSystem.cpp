#include "InputSystem.hpp"

#include "Toast/Input/KeyCodes.hpp"
#include "Toast/Log.hpp"
#include "Toast/ProjectSettings.hpp"
#include "Toast/Window/Window.hpp"
#include "Toast/Window/WindowEvents.hpp"

#include <ranges>

namespace input {

InputSystem* InputSystem::m_instance = nullptr;

InputSystem* InputSystem::get() {
	if (not m_instance) {
		throw ToastException("Tried to access Input System but it's not created yet");
	}
	return m_instance;
}

InputSystem::InputSystem() {
	if (m_instance) {
		throw ToastException("Tried to create Input System but one already exists");
	}
	m_instance = this;

	m.triggerDeadzone = toast::ProjectSettings::input_deadzone();

	// Load all layouts defined in project settings
	const auto& layout_paths = toast::ProjectSettings::input_layouts();
	m.layouts.reserve(layout_paths.size());
	for (const auto& path : layout_paths) {
		auto layout = Layout::create(path);
		if (not layout) {
			TOAST_WARN("Couldn't create layout {}, skipping...", path);
			continue;
		}
		m.layouts.emplace_back(*layout);
	}

	TOAST_INFO("Created {} layouts", m.layouts.size());

	// Check for connected controllers when the game starts
	for (int i = 0; i < 16; i++) {
		// yes im using not and or to spice things up a bit
		if (not glfwJoystickPresent(i) || not glfwJoystickIsGamepad(i)) {
			continue;
		}
		m.controllers[i] = GamepadState {};
		TOAST_INFO("Controller {} connected: {}", i, glfwGetGamepadName(i));
	}

	// No active layout by default
	m.activeLayout = m.layouts.end();

	// Subscribe to window/input events
	m.eventListener.Subscribe<event::WindowKey>([this](auto* e) {
		return OnKeyPress(e);
	});
	m.eventListener.Subscribe<event::WindowMousePosition>([this](auto* e) {
		return OnMousePosition(e);
	});
	m.eventListener.Subscribe<event::WindowMouseButton>([this](auto* e) {
		return OnMouseButton(e);
	});
	m.eventListener.Subscribe<event::WindowMouseScroll>([this](auto* e) {
		return OnMouseScroll(e);
	});
	m.eventListener.Subscribe<event::WindowInputDevice>([this](auto* e) {
		return OnInputDevice(e);
	});
}

void InputSystem::Tick() {
	// Poll controller state and dispatch any queued actions
	PollControllers();
	DispatchQueue(m.dispatch0DQueue, &Listener::M::callbacks0d);
	DispatchQueue(m.dispatch1DQueue, &Listener::M::callbacks1d);
	DispatchQueue(m.dispatch2DQueue, &Listener::M::callbacks2d);
}

#pragma region helper_functions

bool InputSystem::HasActiveLayout() const {
	return m.activeLayout != m.layouts.end();
}

void InputSystem::SetState(std::string_view state) {
	auto* instance = get();
	if (!instance->HasActiveLayout()) {
		return;
	}
	auto& state_list = instance->m.activeLayout->m.states;
	if (std::ranges::find(state_list, state) != state_list.end()) {
		TOAST_WARN("State {} not found on active layout", state);
		instance->m.currentState.clear();
		return;
	}

	instance->m.currentState = state;
}

void InputSystem::ActiveLayout(std::string_view name) {
	auto* instance = get();
	auto& layout_list = instance->m.layouts;
	instance->m.activeLayout = std::ranges::find_if(layout_list, [name](const Layout& l) {
		return l.name == name;
	});

	if (instance->m.activeLayout == layout_list.end()) {
		TOAST_WARN("layout list {} not found", name);
		return;
	}

	// We clear the states so they don;t propagate through input layouts
	instance->m.currentState.clear();
}

void InputSystem::RegisterListener(Listener* ptr) {
	auto* this_ptr = get();
	if (std::ranges::find(this_ptr->m.subscribers, ptr) == this_ptr->m.subscribers.end()) {
		this_ptr->m.subscribers.emplace_back(ptr);
	}
}

void InputSystem::UnregisterListener(Listener* ptr) {
	auto* this_ptr = get();
	auto it = std::find(this_ptr->m.subscribers.begin(), this_ptr->m.subscribers.end(), ptr);
	if (it != this_ptr->m.subscribers.end()) {
		this_ptr->m.subscribers.erase(it);
	}
}

auto InputSystem::GetMousePosition() -> glm::vec2 {
	return get()->m.mousePosition;
}

auto InputSystem::GetMouseDelta() -> glm::vec2 {
	return get()->m.mouseDelta;
}

#pragma endregion

bool InputSystem::Handle0DAction(int key_code, int action, int mods, Device device) {
	// Map button/keypress to 0D actions (boolean-like)
	for (auto& act : m.activeLayout->m.actions0d) {
		if (!act.CheckState(m.currentState)) {
			continue;
		}
		for (const auto& bind : act.m.binds) {
			if (bind.device != device) {
				continue;
			}
			if (!bind.keys.contains(key_code) || action == 2) {
				continue;
			}

			act.device = bind.device;
			act.modifiers = static_cast<ModifierKey>(mods);

			// 0 = release, 1 = press
			if (action == 0) {
				act.m.pressedKeys.erase(key_code);
			} else if (action == 1) {
				act.m.pressedKeys.emplace(key_code, true);
			}

			AddToQueue(m.dispatch0DQueue, &act);
			return true;
		}
	}
	return false;
}

bool InputSystem::Handle1DAction(int key_code, int action, int mods, Device device) {
	// Map button/keypress to 1D actions (float-like)
	for (auto& act : m.activeLayout->m.actions1d) {
		if (!act.CheckState(m.currentState)) {
			continue;
		}
		for (const auto& bind : act.m.binds) {
			if (bind.device != device) {
				continue;
			}
			for (const auto& [key, direction] : bind.keys) {
				if (key != key_code || action == 2) {
					continue;
				}

				act.device = bind.device;
				act.modifiers = static_cast<ModifierKey>(mods);

				if (action == 0) {
					act.m.pressedKeys.erase(key_code);
				} else if (action == 1) {
					act.m.pressedKeys.emplace(key_code, bind.GetFloatValue(direction));
				}

				AddToQueue(m.dispatch1DQueue, &act);
				return true;
			}
		}
	}
	return false;
}

bool InputSystem::Handle2DAction(int key_code, int action, int mods, Device device) {
	// Map button/keypress to 2D actions (vec2-like)
	for (auto& act : m.activeLayout->m.actions2d) {
		if (!act.CheckState(m.currentState)) {
			continue;
		}
		for (const auto& bind : act.m.binds) {
			if (bind.device != device) {
				continue;
			}
			for (const auto& [key, direction] : bind.keys) {
				if (key != key_code || action == 2) {
					continue;
				}

				act.device = bind.device;
				act.modifiers = static_cast<ModifierKey>(mods);

				if (action == 0) {
					act.m.pressedKeys.erase(key_code);
				} else if (action == 1) {
					act.m.pressedKeys.emplace(key_code, bind.GetVec2Value(direction));
				}

				AddToQueue(m.dispatch2DQueue, &act);
				return true;
			}
		}
	}
	return false;
}

bool InputSystem::HandleButtonLikeInput(int key_code, int action, int mods, Device device) {
	// Try each action dimension until one matches
	if (!HasActiveLayout()) {
		return false;
	}
	return Handle0DAction(key_code, action, mods, device) || Handle1DAction(key_code, action, mods, device) ||
	       Handle2DAction(key_code, action, mods, device);
}

bool InputSystem::OnKeyPress(event::WindowKey* e) {
	// Keyboard key press/release
	return HandleButtonLikeInput(e->key, e->action, e->mods, Device::Keyboard);
}

#pragma region mouse

bool InputSystem::OnMouseButton(event::WindowMouseButton* e) {
	// Mouse button press/release
	return HandleButtonLikeInput(e->button, e->action, e->mods, Device::Mouse);
}

bool InputSystem::OnMousePosition(event::WindowMousePosition* e) {
	// Store mouse delta and mouse position
	m.oldMousePosition = m.mousePosition;
	m.mousePosition = glm::vec2 { e->x, e->y };
	m.mouseDelta = m.mousePosition - m.oldMousePosition;

	// Mouse position as a 2D action (normalized to NDC)
	if (!HasActiveLayout()) {
		return false;
	}

	for (auto& action : m.activeLayout->m.actions2d) {
		if (!action.CheckState(m.currentState)) {
			continue;
		}
		for (const auto& bind : action.m.binds) {
			if (bind.device != Device::Mouse) {
				continue;
			}
			for (const auto& [key, _] : bind.keys) {
				if (key == MOUSE_POSITION_CODE) {
					action.device = bind.device;
					action.state = Action2D::Ongoing;

					glm::vec2 value = m.mousePosition;

#if defined(__linux__)
					// Adjust for display scale on Linux
					const auto [sx, sy] = toast::Window::GetInstance()->GetDisplayScale();
					value.x *= sx;
					value.y *= sy;
#endif

					// Convert screen coords to NDC [-1, 1]
					const auto s = toast::Window::GetInstance()->GetFramebufferSize();
					value.x = (value.x / s.x) - 0.5f;
					value.y = (value.y / s.y) - 0.5f;
					value *= 2.0f;
					value.y *= -1.0f;    // y up

					action.m.pressedKeys.emplace(MOUSE_POSITION_CODE, value);
					AddToQueue(m.dispatch2DQueue, &action);
					return true;
				}

				if (key == MOUSE_RAW_CODE) {
					action.device = bind.device;
					action.state = Action2D::Ongoing;
					action.m.pressedKeys.emplace(MOUSE_RAW_CODE, m.mousePosition);
					AddToQueue(m.dispatch2DQueue, &action);
					return true;
				}

				if (key == MOUSE_DELTA_CODE) {
					action.device = bind.device;
					action.state = Action2D::Ongoing;
					action.m.pressedKeys.emplace(MOUSE_DELTA_CODE, m.mouseDelta);
					AddToQueue(m.dispatch2DQueue, &action);
					return true;
				}
			}
		}
	}
	return false;
}

bool InputSystem::HandleScroll0D(event::WindowMouseScroll* e) {
	// Scroll mapped to 0D actions (just ongoing flag)
	for (auto& action : m.activeLayout->m.actions0d) {
		if (!action.CheckState(m.currentState)) {
			continue;
		}
		for (const auto& bind : action.m.binds) {
			if (bind.device != Device::Mouse) {
				continue;
			}
			for (const auto& [key, direction] : bind.keys) {
				if (key != MOUSE_SCROLL_X_CODE && key != MOUSE_SCROLL_Y_CODE) {
					continue;
				}

				action.device = bind.device;
				action.state = Action0D::Ongoing;
				action.m.pressedKeys.emplace(key, true);
				AddToQueue(m.dispatch0DQueue, &action);
				return true;
			}
		}
	}
	return false;
}

bool InputSystem::HandleScroll1D(event::WindowMouseScroll* e) {
	// Scroll mapped to 1D actions (x/y as float)
	for (auto& action : m.activeLayout->m.actions1d) {
		if (!action.CheckState(m.currentState)) {
			continue;
		}
		for (const auto& bind : action.m.binds) {
			if (bind.device != Device::Mouse) {
				continue;
			}
			for (const auto& [key, direction] : bind.keys) {
				if (key == MOUSE_SCROLL_X_CODE) {
					action.device = bind.device;
					action.state = Action1D::Ongoing;
					action.m.pressedKeys.emplace(MOUSE_SCROLL_X_CODE, static_cast<float>(e->x));
					AddToQueue(m.dispatch1DQueue, &action);
					return true;
				}
				if (key == MOUSE_SCROLL_Y_CODE) {
					action.device = bind.device;
					action.state = Action1D::Ongoing;
					action.m.pressedKeys.emplace(MOUSE_SCROLL_Y_CODE, static_cast<float>(e->y));
					AddToQueue(m.dispatch1DQueue, &action);
					return true;
				}
			}
		}
	}
	return false;
}

bool InputSystem::HandleScroll2D(event::WindowMouseScroll* e) {
	// Scroll mapped to 2D actions (x,y packed in vec2)
	for (auto& action : m.activeLayout->m.actions2d) {
		if (!action.CheckState(m.currentState)) {
			continue;
		}
		for (const auto& bind : action.m.binds) {
			if (bind.device != Device::Mouse) {
				continue;
			}
			for (const auto& [key, direction] : bind.keys) {
				if (key != MOUSE_SCROLL_X_CODE && key != MOUSE_SCROLL_Y_CODE) {
					continue;
				}

				action.device = bind.device;
				action.state = Action2D::Ongoing;
				action.m.pressedKeys.emplace(MOUSE_SCROLL_X_CODE, glm::vec2 { e->x, e->y });
				AddToQueue(m.dispatch2DQueue, &action);
				return true;
			}
		}
	}
	return false;
}

bool InputSystem::OnMouseScroll(event::WindowMouseScroll* e) {
	// Route scroll to 0D/1D/2D handlers
	if (!HasActiveLayout()) {
		return false;
	}
	return HandleScroll0D(e) || HandleScroll1D(e) || HandleScroll2D(e);
}

#pragma endregion

#pragma region controller

bool InputSystem::OnInputDevice(event::WindowInputDevice* e) {
	// Track controller connect/disconnect
	if (e->event == GLFW_CONNECTED && glfwJoystickIsGamepad(e->jid)) {
		m.controllers[e->jid] = GamepadState {};
		TOAST_INFO("Controller {0} connected: {1}", e->jid, glfwGetGamepadName(e->jid));
		return true;
	}
	if (e->event == GLFW_DISCONNECTED) {
		TOAST_INFO("Controller {0} disconnected", e->jid);
		m.controllers.erase(e->jid);
		return true;
	}
	return false;
}

void InputSystem::PollControllers() {
	// Skip if no active layout
	if (!HasActiveLayout()) {
		return;
	}

	for (auto& [jid, state] : m.controllers) {
		// Refresh controller state
		state.previous = state.current;
		glfwGetGamepadState(jid, &state.current);

		// Button transitions (press/release)
		for (int i = 0; i <= GLFW_GAMEPAD_BUTTON_LAST; ++i) {
			const bool was_pressed = state.previous.buttons[i] == GLFW_PRESS;
			const bool is_pressed = state.current.buttons[i] == GLFW_PRESS;
			if (!was_pressed && is_pressed) {
				ControllerButton(i, true);
			} else if (was_pressed && !is_pressed) {
				ControllerButton(i, false);
			}
		}

		// Axis changes (with deadzone and normalization)
		std::array<float, 6> axes {};
		for (int i = 0; i <= GLFW_GAMEPAD_AXIS_LAST; ++i) {
			const float prev = std::abs(state.previous.axes[i]) > AXIS_DEADZONE ? state.previous.axes[i] : 0.0f;
			const float curr = std::abs(state.current.axes[i]) > AXIS_DEADZONE ? state.current.axes[i] : 0.0f;
			if (std::abs(curr - prev) > 0.001f) {
				std::ranges::copy(state.current.axes, axes.begin());
				axes[1] *= -1;                        // invert Y on left stick
				axes[3] *= -1;                        // invert Y on right stick
				axes[4] = (axes[4] * 0.5f) + 0.5f;    // trigger L2 to [0,1]
				axes[5] = (axes[5] * 0.5f) + 0.5f;    // trigger R2 to [0,1]
				std::ranges::for_each(axes, [](float& ax) {
					if (std::abs(ax) < AXIS_DEADZONE) {
						ax = 0.0f;
					}
				});
				ControllerAxis(i, axes);
			}
		}
	}
}

void InputSystem::ControllerButton(int id, bool value) {
	// Controller buttons -> 0D
	for (auto& action : m.activeLayout->m.actions0d) {
		if (!action.CheckState(m.currentState)) {
			continue;
		}
		for (const auto& bind : action.m.binds) {
			if (bind.device != Device::ControllerButton) {
				continue;
			}
			if (!bind.keys.contains(id)) {
				continue;
			}
			action.device = bind.device;
			if (!value) {
				action.m.pressedKeys.erase(id + 2e6);
			} else {
				action.m.pressedKeys.emplace(id + 2e6, true);
			}
			AddToQueue(m.dispatch0DQueue, &action);
			return;
		}
	}

	// Controller buttons -> 1D
	for (auto& action : m.activeLayout->m.actions1d) {
		if (!action.CheckState(m.currentState)) {
			continue;
		}
		for (const auto& bind : action.m.binds) {
			if (bind.device != Device::ControllerButton) {
				continue;
			}
			for (const auto& [key, direction] : bind.keys) {
				if (key != id) {
					continue;
				}
				action.device = bind.device;
				if (!value) {
					action.m.pressedKeys.erase(id + 2e6);
				} else {
					action.m.pressedKeys.emplace(id + 2e6, bind.GetFloatValue(direction));
				}
				AddToQueue(m.dispatch1DQueue, &action);
				return;
			}
		}
	}

	// Controller buttons -> 2D
	for (auto& action : m.activeLayout->m.actions2d) {
		if (!action.CheckState(m.currentState)) {
			continue;
		}
		for (const auto& bind : action.m.binds) {
			if (bind.device != Device::ControllerButton) {
				continue;
			}
			for (const auto& [key, direction] : bind.keys) {
				if (key != id) {
					continue;
				}
				action.device = bind.device;
				if (!value) {
					action.m.pressedKeys.erase(id + 2e6);
				} else {
					action.m.pressedKeys.emplace(id + 2e6, bind.GetVec2Value(direction));
				}
				AddToQueue(m.dispatch2DQueue, &action);
				return;
			}
		}
	}
}

// Yeah ignore the clang-tidy warning -x
void InputSystem::ControllerAxis(int id, std::array<float, 6> axes) {
	// Controller axes -> 1D
	for (auto& action : m.activeLayout->m.actions1d) {
		if (!action.CheckState(m.currentState)) {
			continue;
		}
		for (const auto& bind : action.m.binds) {
			if (bind.device != Device::ControllerAxis) {
				continue;
			}
			for (const auto& [key, direction] : bind.keys) {
				if (key != id) {
					continue;
				}
				action.device = bind.device;
				const float value = axes[id];
				if (value != 0.0f) {
					action.m.pressedKeys[id + 2e7] = bind.GetFloatValue(direction) * value;
				} else {
					action.m.pressedKeys.erase(id + 2e7);
				}
				AddToQueue(m.dispatch1DQueue, &action);
				return;
			}
		}
	}

	// Controller axes & sticks -> 2D
	for (auto& action : m.activeLayout->m.actions2d) {
		if (!action.CheckState(m.currentState)) {
			continue;
		}
		for (const auto& bind : action.m.binds) {
			if (bind.device != Device::ControllerAxis && bind.device != Device::ControllerStick) {
				continue;
			}
			for (const auto& [key, direction] : bind.keys) {
				if (bind.device == Device::ControllerAxis && key == id) {
					action.device = bind.device;
					const float value = axes[id];
					if (value != 0.0f) {
						action.m.pressedKeys.emplace(id + 2e7, bind.GetVec2Value(direction) * value);
					} else {
						action.m.pressedKeys.erase(id + 2e7);
					}
					AddToQueue(m.dispatch2DQueue, &action);
					return;
				}
				if (bind.device == Device::ControllerStick) {
					// Left stick (axes 0,1) mapped when key == 0
					if (key == 0 && (id == 0 || id == 1)) {
						action.device = Device::ControllerStick;
						glm::vec2 value = { axes[0], -axes[1] };
						if (abs(value.x) > m.triggerDeadzone || abs(value.y) > m.triggerDeadzone) {
							action.state = Action2D::Ongoing;
							action.m.pressedKeys[id + 2e8] = value;
						} else {
							action.state = Action2D::Finished;
							action.value = { 0.0f, 0.0f };
							action.m.pressedKeys.erase(id + 2e8);
						}
						AddToQueue(m.dispatch2DQueue, &action);
						return;
					}
					// Right stick (axes 2,3) mapped when key == 1
					if (key == 1 && (id == 2 || id == 3)) {
						action.device = Device::ControllerStick;
						glm::vec2 value = { axes[2], -axes[3] };
						if (value != glm::vec2 { 0.0f, 0.0f }) {
							action.m.pressedKeys[id + 2e8] = value;
						} else {
							action.m.pressedKeys.erase(id + 2e8);
						}
						AddToQueue(m.dispatch2DQueue, &action);
						return;
					}
				}
			}
		}
	}
}

#pragma endregion

}
