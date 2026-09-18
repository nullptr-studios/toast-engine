#include "settings.hpp"

#include <ffi/settings.h>

namespace {

using namespace toast::settings;

/// @brief Backing store for the string returns
///
/// A resolved value lives in a variant the next set() may reassign, and a path is a temporary once converted
/// out of std::filesystem. Neither can be handed out by address, so both are copied here first. Thread-local
/// because the editor and the engine both cross this boundary
thread_local std::string g_string_return;

auto entryAt(int32_t index) -> Entry* {
	if (index < 0) {
		return nullptr;
	}
	auto all = Settings::get().entries();
	return static_cast<size_t>(index) < all.size() ? all[static_cast<size_t>(index)] : nullptr;
}

}

extern "C" {

auto toast_settings_count() noexcept -> int32_t {
	return static_cast<int32_t>(Settings::get().entries().size());
}

auto toast_settings_get_desc(int32_t index, toast_setting_desc* out) noexcept -> int32_t {
	const Entry* entry = entryAt(index);
	if (entry == nullptr || out == nullptr) {
		return 0;
	}

	out->key = entry->key.c_str();
	out->label = entry->meta.label.c_str();
	out->category = entry->meta.category.c_str();
	out->description = entry->meta.description.c_str();
	out->type = static_cast<int32_t>(entry->type);
	out->min = entry->meta.min;
	out->max = entry->meta.max;
	out->step = entry->meta.step;
	out->option_count = static_cast<int32_t>(entry->meta.options.size());
	out->requires_restart = entry->meta.requires_restart ? 1 : 0;
	out->hidden = entry->meta.hidden ? 1 : 0;
	return 1;
}

auto toast_settings_get_option(int32_t index, int32_t option) noexcept -> const char* {
	const Entry* entry = entryAt(index);
	if (entry == nullptr || option < 0 || static_cast<size_t>(option) >= entry->meta.options.size()) {
		return nullptr;
	}
	return entry->meta.options[static_cast<size_t>(option)].c_str();
}

auto toast_settings_get_bool(const char* key) noexcept -> int32_t {
	if (key == nullptr) {
		return 0;
	}
	const auto value = Settings::get().value(key);
	if (!value.has_value()) {
		return 0;
	}
	const auto* typed = std::get_if<bool>(&*value);
	return typed != nullptr && *typed ? 1 : 0;
}

auto toast_settings_get_int(const char* key) noexcept -> int64_t {
	if (key == nullptr) {
		return 0;
	}
	const auto value = Settings::get().value(key);
	if (!value.has_value()) {
		return 0;
	}
	const auto* typed = std::get_if<int64_t>(&*value);
	return typed != nullptr ? *typed : 0;
}

auto toast_settings_get_float(const char* key) noexcept -> double {
	if (key == nullptr) {
		return 0.0;
	}
	const auto value = Settings::get().value(key);
	if (!value.has_value()) {
		return 0.0;
	}
	const auto* typed = std::get_if<double>(&*value);
	return typed != nullptr ? *typed : 0.0;
}

auto toast_settings_get_string(const char* key) noexcept -> const char* {
	g_string_return.clear();
	if (key != nullptr) {
		if (const auto value = Settings::get().value(key)) {
			if (const auto* typed = std::get_if<std::string>(&*value)) {
				g_string_return = *typed;
			}
		}
	}
	return g_string_return.c_str();
}

void toast_settings_set_bool(const char* key, int32_t value) noexcept {
	if (key != nullptr) {
		Settings::get().setValue(key, Value {value != 0});
	}
}

void toast_settings_set_int(const char* key, int64_t value) noexcept {
	if (key != nullptr) {
		Settings::get().setValue(key, Value {value});
	}
}

void toast_settings_set_float(const char* key, double value) noexcept {
	if (key != nullptr) {
		Settings::get().setValue(key, Value {value});
	}
}

void toast_settings_set_string(const char* key, const char* value) noexcept {
	if (key != nullptr && value != nullptr) {
		Settings::get().setValue(key, Value {std::string {value}});
	}
}

void toast_settings_reset(const char* key) noexcept {
	if (key != nullptr) {
		Settings::get().reset(key);
	}
}

void toast_settings_reset_all() noexcept {
	Settings::get().resetAll();
}

auto toast_settings_save() noexcept -> int32_t {
	return Settings::get().save() ? 1 : 0;
}

auto toast_settings_is_dirty() noexcept -> int32_t {
	return Settings::get().dirty() ? 1 : 0;
}

auto toast_settings_active_path() noexcept -> const char* {
	g_string_return = Settings::get().activeFilePath().string();
	return g_string_return.c_str();
}
}
