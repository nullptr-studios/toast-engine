/**
 * @file font.hpp
 * @author Xein
 * @date 17 Jul 2026
 *
 * @brief Raw .ttf asset passed to RmlUi
 */

#pragma once
#include <toast/assets/core_types.hpp>
#include <toast/export.hpp>

namespace assets {

class TOAST_API Font : public Asset {
public:
	explicit Font(std::vector<uint8_t> data) : m_data(std::move(data)) { }

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "font";
	}

	[[nodiscard]]
	auto get() const noexcept -> const std::vector<uint8_t>& {
		return m_data;
	}

private:
	std::vector<uint8_t> m_data;
};

}
