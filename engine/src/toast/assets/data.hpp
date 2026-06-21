/**
 * @file data.hpp
 * @author Xein
 * @date 10 Jun 2026
 */

#pragma once
#include "core_types.hpp"

namespace assets {

/**
 * @brief Asset representing parsed TOML data
 */
class TOAST_API Data : public Asset, public ISaveable {
public:
	static constexpr std::string_view collection = "data";

	explicit Data(toml::table table) : m_table(std::move(table)) { }

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "data";
	}

	[[nodiscard]]
	auto get() const noexcept -> const toml::table&;

	[[nodiscard]]
	auto serialize(SaveMode mode) const -> std::vector<uint8_t> override;

private:
	toml::table m_table;
};
}
