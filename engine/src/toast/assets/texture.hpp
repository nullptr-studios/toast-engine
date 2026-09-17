/**
 * @file texture.hpp
 * @author Xein
 * @date 10 Jun 2026
 */

#pragma once
#include "core_types.hpp"

#include <memory>
#include <toast/export.hpp>

namespace renderer {
class VulkanTexture;
}

namespace assets {

/**
 * @brief Asset representing a texture, currently holding raw KTX2 bytes
 */
class TOAST_API Texture : public Asset {
public:
	explicit Texture(std::vector<uint8_t> data);
	~Texture() override;

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "texture";
	}

	[[nodiscard]]
	auto gpuTexture() const -> const renderer::VulkanTexture&;

	[[nodiscard]]
	auto gpuTexture() -> renderer::VulkanTexture&;

private:
	// No CPU-side copy of the encoded bytes
	std::unique_ptr<renderer::VulkanTexture> m_gpu_texture;
};
}
