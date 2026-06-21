/**
 * @file texture.hpp
 * @author Xein
 * @date 10 Jun 2026
 */

#pragma once
#include "core_types.hpp"

namespace assets {

/**
 * @brief Asset representing a texture, currently holding raw KTX2 bytes
 */
class TOAST_API Texture : public Asset {
public:
	static constexpr std::string_view collection = "textures";

	explicit Texture(std::vector<uint8_t> data) : m_data(std::move(data)) { }

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "texture";
	}

	[[nodiscard]]
	auto get() const noexcept -> const std::vector<uint8_t>&;

private:
	std::vector<uint8_t> m_data;
};
}
