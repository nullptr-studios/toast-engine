/// @file InputSystem.hpp
/// @date 12 Dec 2025
/// @author Xein

#pragma once
#include "Toast/Event/ListenerSubNode.hpp"
#include "Toast/Input/InputListener.hpp"
#include "Toast/Input/Layout.hpp"
#include "Toast/Window/WindowEvents.hpp"

#include <algorithm>
#include <array>
#include <glm/glm.hpp>
#include <vector>

namespace input {

struct GamepadState {
	GLFWgamepadstate current;
	GLFWgamepadstate previous;
};

class InputSystem {
public:
	InputSystem();

	[[nodiscard]]
	static InputSystem* get();

	InputSystem(const InputSystem&) = delete;
	InputSystem& operator=(const InputSystem&) = delete;
	InputSystem(InputSystem&&) = delete;
	InputSystem& operator=(InputSystem&&) = delete;

	static void ActiveLayout(std::string_view name);
	static void SetState(std::string_view state);
	static void RegisterListener(Listener* ptr);
	static void UnregisterListener(Listener* ptr);

	static auto GetMousePosition() -> glm::vec2;
	static auto GetMouseDelta() -> glm::vec2;

	static void SetViewportPosition(glm::vec2 position);
	static void SetViewportSize(glm::vec2 size);

	void Tick();

private:
	static InputSystem* m_instance;
	using layout_it = std::vector<Layout>::iterator;
	static constexpr float AXIS_DEADZONE = 0.1f;

	bool HasActiveLayout() const;

	/// @brief Dispatches all actions in the queue to registered listeners
	template<typename ActionType, typename CallbackType>
	void DispatchQueue(std::vector<ActionType*>& queue, CallbackType Listener::M::* map_ptr) {
		PROFILE_ZONE;
		for (auto* a : queue) {
			a->CalculateValue();

			// Call registered callbacks for this action name
			for (auto* l : m.subscribers) {
				auto& callbacks_map = l->m.*map_ptr;
				const auto range = callbacks_map.equal_range(a->name);
				for (auto it = range.first; it != range.second; ++it) {
					it->second(a);
				}
			}

			// Remove one-frame events (mouse scroll/position/delta) after dispatch
			std::erase_if(a->m.pressedKeys, [](const auto& v) {
				return v.first >= MOUSE_DELTA_CODE;
			});

			// Clear the queued flag so it can be re-queued next frame
			a->m.queued = false;
		}
		queue.clear();
	}

	/// @brief Adds an action to the dispatch queue if not already present
	/// O(1) flag check instead of O(n) linear search
	template<typename ActionType>
	void AddToQueue(std::vector<ActionType*>& queue, ActionType* action) {
		if (action->m.queued) {
			return;
		}
		action->m.queued = true;
		queue.emplace_back(action);
	}

	// Event handlers
	bool OnKeyPress(event::WindowKey* e);
	bool OnMousePosition(event::WindowMousePosition* e);
	bool OnMouseButton(event::WindowMouseButton* e);
	bool OnMouseScroll(event::WindowMouseScroll* e);
	bool OnInputDevice(event::WindowInputDevice* e);

	// Shared helpers for buttons/keys (press & release)
	bool HandleButtonLikeInput(int key_code, int action, int mods, Device device);

	bool Handle0DAction(int key_code, int action, int mods, Device device);
	bool Handle1DAction(int key_code, int action, int mods, Device device);
	bool Handle2DAction(int key_code, int action, int mods, Device device);

	// Scroll helpers
	bool HandleScroll0D(event::WindowMouseScroll* e);
	bool HandleScroll1D(event::WindowMouseScroll* e);
	bool HandleScroll2D(event::WindowMouseScroll* e);

	// Controller
	void PollControllers();
	void ControllerButton(int id, bool value);
	void ControllerAxis(int id, const std::array<float, 6>& axes);

	struct M {
		std::vector<Layout> layouts;
		layout_it activeLayout;
		std::string currentState;
		event::ListenerSubNode eventListener;

		std::vector<Listener*> subscribers;
		std::vector<Action0D*> dispatch0DQueue;
		std::vector<Action1D*> dispatch1DQueue;
		std::vector<Action2D*> dispatch2DQueue;

		std::map<int, GamepadState> controllers;

		glm::vec2 oldMousePosition = { 0.0f, 0.0f };
		glm::vec2 mouseDelta = { 0.0f, 0.0f };
		glm::vec2 mousePosition = { 0.0f, 0.0f };

		glm::vec2 viewportSize = { 0.0f, 0.0f };
		glm::vec2 viewportPosition = { 0.0f, 0.0f };

		// Cached inverse VP decomposition for mouse-to-world

		glm::vec3 rayNearOrigin { 0.f };
		glm::vec3 rayNearX { 0.f };
		glm::vec3 rayNearY { 0.f };
		glm::vec3 rayFarOrigin { 0.f };
		glm::vec3 rayFarX { 0.f };
		glm::vec3 rayFarY { 0.f };
		bool mouseRaysDirty = true;

		float triggerDeadzone = 0.2f;
	} m;
};

}
