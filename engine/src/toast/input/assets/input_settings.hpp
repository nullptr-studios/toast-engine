/**
 * @file input_settings.hpp
 * @author Xein
 * @date 21 Jun 2026
 *
 * @brief Asset holding per-player input settings
 */

#pragma once
#include <toast/assets/data.hpp>

namespace assets {

class TOAST_API InputSettings : public Data {
public:
	static constexpr std::string_view collection = "input_settings";

	explicit InputSettings(const toml::table& table, AssetHandle<Schema> schema = {})
	    : Data(table, std::move(schema), Data::KeepAllKeys) { }

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "input_settings";
	}

	/// @return The full settings as a reconstructed TOML table
	[[nodiscard]]
	auto get() const -> toml::table;

	[[nodiscard]]
	auto getFloat(std::string_view key, float fallback) const noexcept -> float;
};

}
