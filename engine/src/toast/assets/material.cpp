#include "material.hpp"

#include "assets.hpp"
#include "texture.hpp"
#include "toast/renderer/core/vulkan_debug.hpp"

#include <algorithm>
#include <format>

namespace assets {

Material::Material(const toml::table& table, AssetHandle<Schema> schema) : Data(table, std::move(schema), Data::keep_all_keys) {
	const auto& d = static_cast<const DataValue&>(m_root);

	if (d.contains("albedo_map")) {
		const toast::UID uid = static_cast<toast::UID>(d["albedo_map"]);
		if (uid.data() != 0) {
			m_albedo_map = assets::load<Texture>(uid);
		}
	}

	if (d.contains("normal_map")) {
		const toast::UID uid = static_cast<toast::UID>(d["normal_map"]);
		if (uid.data() != 0) {
			m_normal_map = assets::load<Texture>(uid);
		}
	}

	if (d.contains("color")) {
		m_color = static_cast<glm::vec4>(d["color"]);
	}
}

auto Material::serialize(SaveMode mode) const -> std::vector<uint8_t> {
	return Data::serialize(mode);
}

void Material::resolveTextureHandles() {
	if (!m_albedo_map.hasValue()) {
		return;
	}

	const auto& core = toast::renderer::getCore();

	vk::SamplerCreateInfo sampler_info {};

	sampler_info.magFilter = vk::Filter::eLinear;
	sampler_info.minFilter = vk::Filter::eLinear;
	sampler_info.addressModeU = vk::SamplerAddressMode::eRepeat;
	sampler_info.addressModeV = vk::SamplerAddressMode::eRepeat;
	sampler_info.addressModeW = vk::SamplerAddressMode::eRepeat;
	// anisotropyEnable=VK_TRUE without the samplerAnisotropy feature enabled on the device is a validation
	// error (VUID-VkSamplerCreateInfo-anisotropyEnable-01070); only request it where it's actually supported
	sampler_info.anisotropyEnable = core.supportsSamplerAnisotropy() ? vk::True : vk::False;
	sampler_info.maxAnisotropy = core.supportsSamplerAnisotropy() ? std::min(16.0f, core.maxSamplerAnisotropy()) : 1.0f;
	sampler_info.compareEnable = vk::False;
	sampler_info.compareOp = vk::CompareOp::eAlways;
	sampler_info.unnormalizedCoordinates = vk::False;

	sampler_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
	sampler_info.mipLodBias = 0.0f;
	sampler_info.minLod = 0.0f;
	sampler_info.maxLod = 0.0f;

	m_albedoSampler = vk::raii::Sampler(core.getDevice(), sampler_info);
	toast::renderer::setDebugName(core, *m_albedoSampler, std::format("Material AlbedoSampler ({})", m_albedo_map.path()));
}

}
