/**
 * @file ui_image.hpp
 * @author Xein
 * @date 17 Jul 2026
 *
 * @brief Raw .tga image used for UI
 */

#pragma once
#include <optional>
#include <toast/assets/core_types.hpp>
#include <toast/export.hpp>
#include <toast/ui/tga_decode.hpp>

namespace assets {

class TOAST_API UIImage : public Asset {
public:
	explicit UIImage(std::vector<uint8_t> data) : m_data(std::move(data)) {
		// We decode the image on load
		m_decoded = ui::decodeTga(m_data);
	}

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "ui_image";
	}

	[[nodiscard]]
	auto get() const noexcept -> const std::vector<uint8_t>& {
		return m_data;
	}

	[[nodiscard]]
	auto decoded() const -> const std::optional<ui::TgaImage>& {
		return m_decoded;
	}

private:
	std::vector<uint8_t> m_data;
	mutable std::optional<ui::TgaImage> m_decoded;
};

}
