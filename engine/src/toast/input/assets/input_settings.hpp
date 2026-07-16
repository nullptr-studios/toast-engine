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
	explicit InputSettings(const toml::table& table, const AssetHandle<Schema>& schema = {})
	    : Data(table, schema, Data::keep_all_keys) { }

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
