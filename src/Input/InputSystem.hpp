/// @file InputSystem.hpp
/// @date 12 Dec 2025
/// @author Xein

#pragma once
#include "Engine/Input/Layout.hpp"
#include "Engine/Input/InputListener.hpp"

#include <Engine/Event/ListenerComponent.hpp>
#include <Engine/Window/WindowEvents.hpp>
#include <algorithm>
#include <list>

namespace input {

struct GamepadState {
	GLFWgamepadstate current;
	GLFWgamepadstate previous;
};

class InputSystem {
public:
	InputSystem();
	[[nodiscard]] static InputSystem* get();

	InputSystem(const InputSystem&) = delete;
	InputSystem& operator=(const InputSystem&) = delete;
	InputSystem(InputSystem&&) = delete;
	InputSystem& operator=(InputSystem&&) = delete;

	static void ActiveLayout(std::string_view name);
	static void SetState(std::string_view state);
	static void RegisterListener(Listener* ptr);
	static void UnregisterListener(Listener* ptr);

	void Tick();

private:
	static InputSystem* m_instance;
	using layout_it = std::vector<Layout>::iterator;
	static constexpr float AXIS_DEADZONE = 0.1f;

	bool HasActiveLayout() const;

	template<typename ActionType, typename CallbackType>
	void DispatchQueue(std::deque<ActionType*>& queue, CallbackType Listener::M::* map_ptr) {
		while (!queue.empty()) {
			ActionType* a = queue.front();
			const auto& name = a->name;

			a->CalculateValue();
			std::erase_if(a->m.pressedKeys, [](const auto& v){
				return v.first == MOUSE_SCROLL_Y_CODE
					|| v.first == MOUSE_SCROLL_X_CODE
					|| v.first == MOUSE_POSITION_CODE;
			});

			for (auto* l : m.subscribers) {
				auto& callbacks_map = l->m.*map_ptr;
				const auto range = callbacks_map.equal_range(name);
				for (auto it = range.first; it != range.second; ++it) {
					it->second(a);
				}
			}
			queue.pop_front();
		}
	}

	template<typename ActionType>
	void AddToQueue(std::deque<ActionType*>& queue, ActionType* action) {
		if (std::ranges::find(queue, action) != queue.end()) return;
		queue.emplace_back(action);
	}

	// Event handlers
	bool OnKeyPress(event::WindowKey* e);
	bool OnMousePosition(event::WindowMousePosition* e);
	bool OnMouseButton(event::WindowMouseButton* e);
	bool OnMouseScroll(event::WindowMouseScroll* e);
	bool OnInputDevice(event::WindowInputDevice* e);

	// Shared helpers for buttons/keys (press & release)
	bool HandleButtonLikeInput(int key_code, int action, int mods);

	bool Handle0DAction(int key_code, int action, int mods);
	bool Handle1DAction(int key_code, int action, int mods);
	bool Handle2DAction(int key_code, int action, int mods);

	// Scroll helpers
	bool HandleScroll0D(event::WindowMouseScroll* e);
	bool HandleScroll1D(event::WindowMouseScroll* e);
	bool HandleScroll2D(event::WindowMouseScroll* e);

	// Controller
	void PollControllers();
	void ControllerButton(int id, bool value);
	void ControllerAxis(int id, std::array<float, 6> axes);

	struct M {
		std::vector<Layout> layouts;
		layout_it activeLayout;
		std::string currentState;
		event::ListenerComponent eventListener;

		std::list<Listener*> subscribers;
		std::deque<Action0D*> dispatch0DQueue;
		std::deque<Action1D*> dispatch1DQueue;
		std::deque<Action2D*> dispatch2DQueue;

		std::map<int, GamepadState> controllers;
	} m;
};

}
