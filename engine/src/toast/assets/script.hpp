/**
 * @file script.hpp
 * @author Xein
 * @date 10 Jul 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "core_types.hpp"

namespace assets {

class Script : public Asset {
public:
	explicit Script(std::vector<uint8_t> data) : m_data(std::move(data)) { }

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "script";
	}

	[[nodiscard]]
	auto get() const noexcept -> const std::vector<uint8_t>& {
		return m_data;
	}

private:
	std::vector<uint8_t> m_data;
};

}
