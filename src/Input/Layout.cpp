#include "Engine/Input/Layout.hpp"

namespace input {

auto Layout::create(const std::string& path) noexcept -> std::optional<Layout> {
	Layout layout;
	sol::state lua;
	sol::table lua_table;

	try {
		// Loading the lua file
		lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::table);

		std::string current_path = lua["package"]["path"];
		std::string custom_path = ";./assets/?.lua;./assets/layouts/?.lua";
		lua["package"]["path"] = current_path + custom_path;

		auto file = resource::Open(path);
		if (!file.has_value()) {
			TOAST_ERROR("Input layout file couldn't be open");
			return std::nullopt;
		}

		sol::optional<sol::table> result = lua.script(*file);
		if (!result.has_value()) {
			TOAST_ERROR("Input layout file didn't return anything");
			return std::nullopt;
		}
		lua_table = *result;
	} catch (const sol::error& e) {
		TOAST_ERROR("Input layout file failed to compile: {}", e.what());
		return std::nullopt;
	}

	// Checking the format
	sol::optional<std::string> format = lua_table["format"];
	if (!format.has_value() || *format != "input_layout") {
		TOAST_ERROR("Input layout doesn't have the correct format");
		return std::nullopt;
	}

	// Add all the states
	layout.name = lua_table["name"].get_or<std::string>("Unnamed");

	layout.m.states.reserve(lua_table["states"].get<sol::table>().size());
	for (auto& [_, state] : lua_table["states"].get<sol::table>()) {
		if (!state.is<std::string>()) {
			continue;
		}
		std::string state_str = state.as<std::string>();
		layout.m.states.push_back(state_str);
	}

	TOAST_TRACE("Added {} states", layout.m.states.size());

	// Add all the actions
	sol::table actions = lua_table["actions"].get_or_create<sol::table>();
	if (actions.empty()) {
		return std::nullopt;
	}
	for (auto& i : actions) {
		std::string name = i.first.as<std::string>();
		sol::table table = i.second.as<sol::table>();
		sol::optional<int> type = table["type"];
		if (!type) {
			continue;
		}

		// NOLINTBEGIN
		switch (*type) {
			case 0: {
				auto action = Action0D::create(name, table);
				if (!action.has_value()) {
					goto action_error;
				}
				layout.m.actions0d.emplace_back(*action);
				break;
			}
			case 1: {
				auto action = Action1D::create(name, table);
				if (!action.has_value()) {
					goto action_error;
				}
				layout.m.actions1d.emplace_back(*action);
				break;
			}
			case 2: {
				auto action = Action2D::create(name, table);
				if (!action.has_value()) {
					goto action_error;
				}
				layout.m.actions2d.emplace_back(*action);
				break;
			}
			default: continue;
		}
		// NOLINTEND

		continue;

action_error:
		TOAST_WARN("Couldn't create action {}, skipping...", name);
	}

	TOAST_INFO("Created layout \"{}\"", layout.name);
	return layout;
}

}
