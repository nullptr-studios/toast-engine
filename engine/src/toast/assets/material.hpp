/**
 * @file Material.hpp
 * @author Xein
 * @date 12 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "core_types.hpp"

namespace asset {
/**
 * @brief Asset representing parsed TOML data
 */
class TOAST_API Material : public assets::Asset {
public:
	explicit Material(toml::table table) : m_table(std::move(table)) { }

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "material";
	}

	[[nodiscard]]
	auto get() const noexcept -> const toml::v3::table&;

private:
	toml::table m_table;
};
}
