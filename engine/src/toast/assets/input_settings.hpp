/**
 * @file input_settings.hpp
 * @author Xein
 * @date 21 Jun 2026
 *
 * @brief Asset holding per-player input settings
 */

#pragma once
#include "core_types.hpp"

namespace assets {

class TOAST_API InputSettings : public Asset, public ISaveable {
public:
	static constexpr std::string_view collection = "input_settings";

	explicit InputSettings(toml::table table) : m_table(std::move(table)) { }

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "input_settings";
	}

	/// @return The full parsed settings table
	[[nodiscard]]
	auto get() const noexcept -> const toml::table&;

	[[nodiscard]]
	auto getFloat(std::string_view key, float fallback) const noexcept -> float;

	[[nodiscard]]
	auto serialize(SaveMode mode) const -> std::vector<uint8_t> override;

private:
	toml::table m_table;
};

}
