#include "Toast/Input/Bind.hpp"

#include "Toast/Log.hpp"

#include <optional>

using namespace input;

auto Bind::create(const sol::object& obj) -> std::optional<Bind> {
	if (obj.is<std::string>()) {
		auto str = obj.as<std::string>();
		return create(str);
	}

	if (obj.is<sol::table>()) {
		return create(obj.as<sol::table>());
	}

	TOAST_ERROR("Bind object is not valid");
	return std::nullopt;
}

auto Bind::create(std::string& key) -> std::optional<Bind> {
	Bind bind;

	auto key_data = KeycodeFromString(key);
	if (!key_data.has_value()) {
		TOAST_ERROR("Keycode {} is invalid", key);
		return std::nullopt;
	}
	auto [code, device] = key_data.value();

	bind.device = device;
	bind.keys.insert({ code, BindRange::Full });
	return bind;
}

auto Bind::create(const sol::table& table) -> std::optional<Bind> {
	Bind bind;

	// Try inserting binds
	bind.Insert(table["key"], BindRange::Full);
	bind.Insert(table["up"], BindRange::Up);
	bind.Insert(table["down"], BindRange::Down);
	bind.Insert(table["left"], BindRange::Left);
	bind.Insert(table["right"], BindRange::Right);
	bind.Insert(table["x"], BindRange::x);
	bind.Insert(table["y"], BindRange::y);

	// If we couldn't insert any key we fail creation
	if (bind.keys.empty()) {
		TOAST_ERROR("All keycodes in table were invalid");
		return std::nullopt;
	}
	return bind;
}

void Bind::Insert(const sol::object& key_lua, BindRange range) {
	// We ignore the insert if the key is invalid
	if (!key_lua.valid() || key_lua.get_type() != sol::type::string) {
		// TOAST_WARN("Keycode is not a Lua String, skipping..."); no need to log this
		return;
	}

	// Try getting the keycode, skip if not found
	std::string str = key_lua.as<std::string>();
	auto key_data = KeycodeFromString(str);
	if (!key_data.has_value()) {
		TOAST_WARN("Keycode {} is invalid, skipping...", str);
		return;
	}

	auto [key_code, device] = key_data.value();
	this->device = device;
	keys.insert({ key_code, range });
}
