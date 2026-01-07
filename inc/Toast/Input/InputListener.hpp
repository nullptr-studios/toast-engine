/// @file InputListener.hpp
/// @date 15 Dec 2025
/// @author Xein

#pragma once

#include "Action.hpp"

#include <Toast/Log.hpp>
#include <functional>
#include <string>
#include <unordered_map>

namespace input {

class InputSystem;

class Listener {
public:
	Listener();
	virtual ~Listener();

	using Callback0D = std::function<void(const Action0D*)>;
	using Callback1D = std::function<void(const Action1D*)>;
	using Callback2D = std::function<void(const Action2D*)>;

	void Subscribe0D(const std::string& name, Callback0D&& callback);
	void Subscribe1D(const std::string& name, Callback1D&& callback);
	void Subscribe2D(const std::string& name, Callback2D&& callback);

	void Unsubscribe0D(const std::string& name);
	void Unsubscribe1D(const std::string& name);
	void Unsubscribe2D(const std::string& name);

private:
	friend class InputSystem;

	struct M {
		std::unordered_multimap<std::string, Callback0D> callbacks0d;
		std::unordered_multimap<std::string, Callback1D> callbacks1d;
		std::unordered_multimap<std::string, Callback2D> callbacks2d;
	} m;
};

void SetLayout(std::string_view name);    ///< @brief Change the current input layout
void SetState(std::string_view state);    ///< @brief Change the current input state

}
