/**
 * @file ui_style.hpp
 * @author Xein
 * @date 17 Jul 2026
 *
 * @brief Raw .rcss style sheet asset
 */

#pragma once
#include <string>
#include <string_view>
#include <toast/assets/core_types.hpp>
#include <toast/export.hpp>

namespace assets {

class TOAST_API UIStyle : public Asset {
public:
	explicit UIStyle(std::vector<uint8_t> data) : m_source(reinterpret_cast<const char*>(data.data()), data.size()) { }

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "ui_style";
	}

	[[nodiscard]]
	auto source() const noexcept -> std::string_view {
		return m_source;
	}

	void setSource(std::vector<uint8_t> data) { m_source.assign(reinterpret_cast<const char*>(data.data()), data.size()); }

private:
	std::string m_source;
};

}
