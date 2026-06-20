#include "InputSystem.hpp"

#include "KeyCodes.hpp"

#include <SDL3/SDL_gamepad.h>
#include <toast/log.hpp>
#include <toast/window/base_window.hpp>
#include <toast/window/window_events.hpp>
#include <tracy/Tracy.hpp>

namespace input {

InputSystem* InputSystem::m_instance = nullptr;

InputSystem* InputSystem::get() {
	if (not m_instance) {
		throw ToastException("Tried to access Input System but it's not created yet");
	}
	return m_instance;
}

InputSystem::InputSystem() {
	PROFILE_ZONE_N("Input system construction");

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
	int gamepad_count = 0;
	SDL_JoystickID* gamepads = SDL_GetGamepads(&gamepad_count);
	if (gamepads) {
		for (int i = 0; i < gamepad_count; ++i) {
			SDL_Gamepad* handle = SDL_OpenGamepad(gamepads[i]);
			if (!handle) {
				continue;
			}
			m.controllers[static_cast<int>(gamepads[i])] = GamepadState {.handle = handle};
			TOAST_INFO("Controller {} connected: {}", gamepads[i], SDL_GetGamepadName(handle));
		}
		SDL_free(gamepads);
	}

	// No active layout by default
	m.activeLayout = m.layouts.end();

	// Subscribe to window/input events
	m.eventListener.Subscribe<event::WindowKey>([this](auto* e) { return OnKeyPress(e); });
	m.eventListener.Subscribe<event::WindowMousePosition>([this](auto* e) { return OnMousePosition(e); });
	m.eventListener.Subscribe<event::WindowMouseButton>([this](auto* e) { return OnMouseButton(e); });
	m.eventListener.Subscribe<event::WindowMouseScroll>([this](auto* e) { return OnMouseScroll(e); });
	m.eventListener.Subscribe<event::WindowInputDevice>([this](auto* e) { return OnInputDevice(e); });
}

void InputSystem::Tick() {
	PROFILE_ZONE;
	m.mouseRaysDirty = true;

	// Check all connected controllers for button/axis changes
	PollControllers();

	// For held keys, we need to dispatch Ongoing events every frame
	// Add all actions with pressed keys to the dispatch queue
	if (HasActiveLayout()) {
		// 0D actions that are held
		for (auto& action : m.activeLayout->m.actions0d) {
			if (!action.m.pressedKeys.empty() && action.CheckState(m.currentState)) {
				AddToQueue(m.dispatch0DQueue, &action);
			}
		}

		// 1D actions that are held
		for (auto& action : m.activeLayout->m.actions1d) {
			if (!action.m.pressedKeys.empty() && action.CheckState(m.currentState)) {
				AddToQueue(m.dispatch1DQueue, &action);
			}
		}

		// 2D actions that are held
		for (auto& action : m.activeLayout->m.actions2d) {
			if (!action.m.pressedKeys.empty() && action.CheckState(m.currentState)) {
				AddToQueue(m.dispatch2DQueue, &action);
			}
		}
	}

	// Dispatch all queued actions
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
	if (std::find(state_list.begin(), state_list.end(), state) == state_list.end()) {
		TOAST_WARN("State {} not found on active layout", state);
		instance->m.currentState.clear();
		return;
	}

	instance->m.currentState = state;
}

void InputSystem::ActiveLayout(std::string_view name) {
	auto* instance = get();
	auto& layout_list = instance->m.layouts;

	// Initialize all actions to null when loading
	// If we do not do this, actions can be left ongoing if switched layout mid action
	if (instance->HasActiveLayout()) {
		for (auto& a : instance->m.activeLayout->m.actions0d) {
			a.m.pressedKeys.clear();
			a.state = Action0D::Null;
		}
		for (auto& a : instance->m.activeLayout->m.actions1d) {
			a.m.pressedKeys.clear();
			a.state = Action1D::Null;
		}
		for (auto& a : instance->m.activeLayout->m.actions2d) {
			a.m.pressedKeys.clear();
			a.state = Action2D::Null;
		}
	}

	instance->m.activeLayout =
	    std::find_if(layout_list.begin(), layout_list.end(), [name](const Layout& l) { return l.name == name; });

	if (instance->m.activeLayout == layout_list.end()) {
		TOAST_WARN("layout list {} not found", name);
		return;
	}

	// We clear the states so they don;t propagate through input layouts
	instance->m.currentState.clear();
}

void InputSystem::RegisterListener(Listener* ptr) {
	auto* this_ptr = get();
	if (std::find(this_ptr->m.subscribers.begin(), this_ptr->m.subscribers.end(), ptr) == this_ptr->m.subscribers.end()) {
		this_ptr->m.subscribers.emplace_back(ptr);
	}
}

void InputSystem::UnregisterListener(Listener* ptr) {
	auto* this_ptr = get();
	auto& subs = this_ptr->m.subscribers;
	auto it = std::find(subs.begin(), subs.end(), ptr);
	if (it != subs.end()) {
		// Swap with last element and pop â€” O(1), order doesn't matter
		*it = subs.back();
		subs.pop_back();
	}
}

auto InputSystem::GetMousePosition() -> glm::vec2 {
	return get()->m.mousePosition;
}

auto InputSystem::GetMouseDelta() -> glm::vec2 {
	return get()->m.mouseDelta;
}

auto InputSystem::GetFirstConnectedGamepad() -> SDL_Gamepad* {
	auto* instance = get();
	// Return first gamepad that's still connected
	for (auto& [jid, state] : instance->m.controllers) {
		if (state.handle && SDL_GamepadConnected(state.handle)) {
			return state.handle;
		}
	}
	return nullptr;
}

auto InputSystem::GetGamepadType() -> SDL_GamepadType {
	auto* gamepad = GetFirstConnectedGamepad();
	if (not gamepad) {
		return SDL_GAMEPAD_TYPE_UNKNOWN;
	}
	return SDL_GetGamepadType(gamepad);
}

void InputSystem::SetViewportPosition(glm::vec2 position) {
	get()->m.viewportPosition = position;
}

void InputSystem::SetViewportSize(glm::vec2 size) {
	get()->m.viewportSize = size;
}

#pragma endregion

bool InputSystem::Handle0DAction(int key_code, int action, int mods, Device device) {
	// Ignore OS key repeat events (action == 2 = repeated)
	// We handle held keys ourselves by dispatching every frame in Tick()
	if (action == 2) {
		return false;
	}

	// Map button/keypress to 0D actions (boolean-like)
	for (auto& act : m.activeLayout->m.actions0d) {
		if (!act.CheckState(m.currentState)) {
			continue;
		}
		for (const auto& bind : act.m.binds) {
			if (bind.device != device) {
				continue;
			}
			if (!bind.ContainsKey(key_code)) {
				continue;
			}

			act.device = bind.device;
			act.modifiers = static_cast<ModifierKey>(mods);

			// action: 0 = release, 1 = press
			if (action == 0) {
				// Key released - remove from pressed keys
				act.m.pressedKeys.erase(key_code);
			} else if (action == 1) {
				// Key pressed - add to pressed keys
				act.m.pressedKeys.emplace(key_code, true);
			}

			// Add to queue - will be dispatched in Tick()
			AddToQueue(m.dispatch0DQueue, &act);
			return true;
		}
	}
	return false;
}

bool InputSystem::Handle1DAction(int key_code, int action, int mods, Device device) {
	// Ignore OS key repeat events
	if (action == 2) {
		return false;
	}

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
				if (key != key_code) {
					continue;
				}

				act.device = bind.device;
				act.modifiers = static_cast<ModifierKey>(mods);

				if (action == 0) {
					// Key released
					act.m.pressedKeys.erase(key_code);
				} else if (action == 1) {
					// Key pressed - store the directional value
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
	// Ignore OS key repeat events
	if (action == 2) {
		return false;
	}

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
				if (key != key_code) {
					continue;
				}

				act.device = bind.device;
				act.modifiers = static_cast<ModifierKey>(mods);

				if (action == 0) {
					// Key released
					act.m.pressedKeys.erase(key_code);
				} else if (action == 1) {
					// Key pressed - store the directional vec2 value
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
	PROFILE_ZONE;
	// Store mouse delta and mouse position
	m.oldMousePosition = m.mousePosition;
	m.mousePosition = glm::vec2 {e->x, e->y};
	m.mouseDelta = m.mousePosition - m.oldMousePosition;

	// Mouse position as a 2D action (normalized to NDC)
	// NOTE: Mouse position should NOT have Started/Ongoing states like keys
	// It's a continuous value that updates every frame
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
				// Mouse position in normalized device coordinates [-1, 1]
				if (key == MOUSE_POSITION_CODE) {
					action.device = bind.device;

					// Localize mouse position to viewport and convert to NDC [-1, 1]
					glm::vec2 localMouse = m.mousePosition - m.viewportPosition;
					float ndcX = (localMouse.x * 2.0f / m.viewportSize.x) - 1.0f;
					float ndcY = 1.0f - (localMouse.y * 2.0f / m.viewportSize.y);

					auto* renderer = renderer::IRendererBase::GetInstance();
					auto* cam = renderer->GetActiveCamera();

					if (cam) {
						if (m.mouseRaysDirty) {
							m.mouseRaysDirty = false;

							glm::mat4 invVP = glm::inverse(renderer->GetProjectionMatrix() * cam->GetViewMatrix());

							// Precompute: world = invVP * (nx, ny, z, 1) / w
							// For z = -1 (near) and z = +1 (far), expand into origin + x_coeff * nx + y_coeff * ny
							auto unprojectCorner = [&](float nx, float ny, float nz) -> glm::vec3 {
								glm::vec4 h = invVP * glm::vec4(nx, ny, nz, 1.0f);
								return glm::vec3(h) / h.w;
							};

							glm::vec3 n00 = unprojectCorner(0.0f, 0.0f, -1.0f);
							glm::vec3 n10 = unprojectCorner(1.0f, 0.0f, -1.0f);
							glm::vec3 n01 = unprojectCorner(0.0f, 1.0f, -1.0f);
							m.rayNearOrigin = n00;
							m.rayNearX = n10 - n00;
							m.rayNearY = n01 - n00;

							glm::vec3 f00 = unprojectCorner(0.0f, 0.0f, 1.0f);
							glm::vec3 f10 = unprojectCorner(1.0f, 0.0f, 1.0f);
							glm::vec3 f01 = unprojectCorner(0.0f, 1.0f, 1.0f);
							m.rayFarOrigin = f00;
							m.rayFarX = f10 - f00;
							m.rayFarY = f01 - f00;
						}

						// Fast per-move: reconstruct near/far world points from cached coefficients
						glm::vec3 nearW = m.rayNearOrigin + m.rayNearX * ndcX + m.rayNearY * ndcY;
						glm::vec3 farW = m.rayFarOrigin + m.rayFarX * ndcX + m.rayFarY * ndcY;

						// Intersect ray with Z=0 world plane
						glm::vec3 dir = farW - nearW;
						float t = (std::abs(dir.z) > 0.0001f) ? (-nearW.z / dir.z) : 0.0f;
						glm::vec3 worldPos = nearW + dir * t;

						action.m.pressedKeys[MOUSE_RAW_CODE] = glm::vec2(worldPos.x, worldPos.y);
					}

					AddToQueue(m.dispatch2DQueue, &action);
					return true;
				}

				// Raw mouse position (screen coordinates)
				if (key == MOUSE_RAW_CODE) {
					action.device = bind.device;
					action.m.pressedKeys[MOUSE_RAW_CODE] = m.mousePosition;
					AddToQueue(m.dispatch2DQueue, &action);
					return true;
				}

				// Mouse delta (movement since last frame)
				if (key == MOUSE_DELTA_CODE) {
					action.device = bind.device;
					action.m.pressedKeys[MOUSE_DELTA_CODE] = m.mouseDelta;
					AddToQueue(m.dispatch2DQueue, &action);
					return true;
				}
			}
		}
	}
	return false;
}

bool InputSystem::HandleScroll0D(event::WindowMouseScroll* e) {
	// Scroll mapped to 0D actions (one-frame boolean event)
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
				// Store as transient input (will be cleared after dispatch)
				action.m.pressedKeys[key] = true;
				AddToQueue(m.dispatch0DQueue, &action);
				return true;
			}
		}
	}
	return false;
}

bool InputSystem::HandleScroll1D(event::WindowMouseScroll* e) {
	// Scroll mapped to 1D actions (x/y as float, one-frame event)
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
					// Store as transient input (will be cleared after dispatch)
					action.m.pressedKeys[MOUSE_SCROLL_X_CODE] = static_cast<float>(e->x);
					AddToQueue(m.dispatch1DQueue, &action);
					return true;
				}
				if (key == MOUSE_SCROLL_Y_CODE) {
					action.device = bind.device;
					action.m.pressedKeys[MOUSE_SCROLL_Y_CODE] = static_cast<float>(e->y);
					AddToQueue(m.dispatch1DQueue, &action);
					return true;
				}
			}
		}
	}
	return false;
}

bool InputSystem::HandleScroll2D(event::WindowMouseScroll* e) {
	// Scroll mapped to 2D actions (x,y packed in vec2, one-frame event)
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
				// Store as transient input (will be cleared after dispatch)
				action.m.pressedKeys[MOUSE_SCROLL_X_CODE] = glm::vec2 {e->x, e->y};
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
	if (e->event == event::WINDOW_INPUT_DEVICE_CONNECTED) {
		SDL_Gamepad* handle = SDL_OpenGamepad(static_cast<SDL_JoystickID>(e->jid));
		if (!handle) {
			return false;
		}
		m.controllers[e->jid] = GamepadState {.handle = handle};
		TOAST_INFO("Controller {0} connected: {1}", e->jid, SDL_GetGamepadName(handle));
		return true;
	}
	if (e->event == event::WINDOW_INPUT_DEVICE_DISCONNECTED) {
		TOAST_INFO("Controller {0} disconnected", e->jid);
		auto it = m.controllers.find(e->jid);
		if (it != m.controllers.end() && it->second.handle) {
			SDL_CloseGamepad(it->second.handle);
		}
		m.controllers.erase(e->jid);
		return true;
	}
	return false;
}

void InputSystem::PollControllers() {
	PROFILE_ZONE;
	// Skip if no active layout
	if (!HasActiveLayout()) {
		return;
	}

	for (auto& [jid, state] : m.controllers) {
		if (!state.handle || !SDL_GamepadConnected(state.handle)) {
			continue;
		}

		state.previousButtons = state.currentButtons;
		state.previousAxes = state.currentAxes;

		for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i) {
			state.currentButtons[i] = SDL_GetGamepadButton(state.handle, static_cast<SDL_GamepadButton>(i));
		}
		for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i) {
			const Sint16 raw = SDL_GetGamepadAxis(state.handle, static_cast<SDL_GamepadAxis>(i));
			state.currentAxes[i] = std::clamp(static_cast<float>(raw) / 32767.0f, -1.0f, 1.0f);
		}

		// Button transitions (press/release)
		for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i) {
			const bool was_pressed = state.previousButtons[i];
			const bool is_pressed = state.currentButtons[i];
			if (!was_pressed && is_pressed) {
				ControllerButton(i, true);
			} else if (was_pressed && !is_pressed) {
				ControllerButton(i, false);
			}
		}

		// Build transformed axes once for both current and previous states
		// and compare processed values so release detection matches runtime behavior.
		auto transform_axes = [&](const std::array<float, SDL_GAMEPAD_AXIS_COUNT>& src) {
			std::array<float, SDL_GAMEPAD_AXIS_COUNT> out {};
			std::copy(std::begin(src), std::end(src), out.begin());

			// Invert Y axes to match standard coordinate system (up = positive)
			out[1] *= -1.0f;    // Left stick Y
			out[3] *= -1.0f;    // Right stick Y

			// SDL triggers already report [0, 1], so don't remap from [-1, 1].
			for (auto& ax : out) {
				if (std::abs(ax) < AXIS_DEADZONE) {
					ax = 0.0f;
				}
			}
			return out;
		};

		const auto previous_axes = transform_axes(state.previousAxes);
		const auto axes = transform_axes(state.currentAxes);

		bool any_axis_changed = false;
		for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i) {
			if (std::abs(axes[i] - previous_axes[i]) > 0.001f) {
				any_axis_changed = true;
				break;
			}
		}

		if (!any_axis_changed) {
			continue;
		}

		// Now dispatch only the axes that actually changed
		for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i) {
			if (std::abs(axes[i] - previous_axes[i]) > 0.001f) {
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
			if (!bind.ContainsKey(id)) {
				continue;
			}
			action.device = bind.device;
			if (!value) {
				action.m.pressedKeys.erase(id + 2000000);
			} else {
				action.m.pressedKeys.emplace(id + 2000000, true);
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
					action.m.pressedKeys.erase(id + 2000000);
				} else {
					action.m.pressedKeys.emplace(id + 2000000, bind.GetFloatValue(direction));
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
					action.m.pressedKeys.erase(id + 2000000);
				} else {
					action.m.pressedKeys.emplace(id + 2000000, bind.GetVec2Value(direction));
				}
				AddToQueue(m.dispatch2DQueue, &action);
				return;
			}
		}
	}
}

// Yeah ignore the clang-tidy warning -x
void InputSystem::ControllerAxis(int id, const std::array<float, SDL_GAMEPAD_AXIS_COUNT>& axes) {
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

				// Key offset: Add large constant to avoid collision with other input types
				const int key_code = id + static_cast<int>(2e7);

				if (value != 0.0f) {
					// Axis is active - store the scaled value
					action.m.pressedKeys[key_code] = bind.GetFloatValue(direction) * value;
				} else {
					// Axis returned to neutral - clear it
					action.m.pressedKeys.erase(key_code);
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

					// Key offset for single axis
					const int key_code = id + static_cast<int>(2e7);

					if (value != 0.0f) {
						// Store scaled 2D value
						action.m.pressedKeys[key_code] = bind.GetVec2Value(direction) * value;
					} else {
						// Clear when axis returns to neutral
						action.m.pressedKeys.erase(key_code);
					}
					AddToQueue(m.dispatch2DQueue, &action);
					return;
				}
				if (bind.device == Device::ControllerStick) {
					// Left Stick: axes 0 (X) and 1 (Y)
					// Key 0 indicates left stick binding
					if (key == 0 && (id == 0 || id == 1)) {
						action.device = Device::ControllerStick;

						// Get individual axis values
						// Note: axes[1] is already inverted in PollControllers()
						const float x_value = axes[0];
						const float y_value = axes[1];

						const int key_code = static_cast<int>(2e8);

						// Check if stick is outside deadzone (any axis active)
						if (std::abs(x_value) > m.triggerDeadzone || std::abs(y_value) > m.triggerDeadzone) {
							// Stick is active - store the full 2D vector as a single entry
							// This ensures accurate analog values without artificial clamping
							action.m.pressedKeys[key_code] = glm::vec2 {x_value, y_value};
						} else {
							// Stick returned to neutral, clear the entry
							action.m.pressedKeys.erase(key_code);
						}
						AddToQueue(m.dispatch2DQueue, &action);
						return;
					}
					// Right stick (axes 2,3) mapped when key == 1
					if (key == 1 && (id == 2 || id == 3)) {
						action.device = Device::ControllerStick;

						// Get individual axis values
						// Note: axes[3] is already inverted in PollControllers()
						const float x_value = axes[2];
						const float y_value = axes[3];

						// Use a single key for the stick
						const int key_code = static_cast<int>(2e8) + 1;

						// Check if stick is outside deadzone (any axis active)
						if (std::abs(x_value) > m.triggerDeadzone || std::abs(y_value) > m.triggerDeadzone) {
							// Stick is active - store the full 2D vector as a single entry
							action.m.pressedKeys[key_code] = glm::vec2 {x_value, y_value};
						} else {
							// Stick returned to neutral - clear the entry
							action.m.pressedKeys.erase(key_code);
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
