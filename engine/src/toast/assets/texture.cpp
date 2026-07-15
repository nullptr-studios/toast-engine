#include "texture.hpp"

#include "toast/log.hpp"
#include "toast/renderer/vulkan_renderer.hpp"
#include "toast/renderer/vulkan_texture.hpp"

namespace assets {

Texture::Texture(std::vector<uint8_t> data)
    : m_data(std::move(data)),
      m_gpu_texture(std::make_unique<toast::renderer::VulkanTexture>()) {
	if (toast::renderer::VulkanRenderer::instance == nullptr) {
		TOAST_WARN("Texture", "VulkanRenderer is not available; texture '{}' was loaded without GPU upload", type());
		return;
	}

	toast::renderer::queueResourceUpload(std::make_unique<toast::renderer::TextureUpload>(*m_gpu_texture, m_data));
}

Texture::~Texture() = default;

auto Texture::gpuTexture() const -> const toast::renderer::VulkanTexture& {
	return *m_gpu_texture;
}

auto Texture::gpuTexture() -> toast::renderer::VulkanTexture& {
	return *m_gpu_texture;
}

}
