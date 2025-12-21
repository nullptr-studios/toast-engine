/// @file Layout.hpp
/// @date 12 Dec 2025
/// @author Xein

#pragma once
#include "Action.hpp"

#include <Engine/Resources/ResourceManager.hpp>
#include <optional>
#include <sol/sol.hpp>

namespace input {

class Layout {
public:
	Layout(Layout&) = default;
	Layout(Layout&&) = default;
	static auto create(const std::string& path) noexcept -> std::optional<Layout>;

	std::string name;

private:
	friend class InputSystem;
	Layout() = default;

	struct M {
		std::vector<std::string> states;
		std::vector<Action0D> actions0d;
		std::vector<Action1D> actions1d;
		std::vector<Action2D> actions2d;
	} m;
};

}
