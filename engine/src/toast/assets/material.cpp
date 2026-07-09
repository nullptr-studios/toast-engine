#include "material.hpp"

#include "assets.hpp"
#include "texture.hpp"
#include "toast/renderer/core/vulkan_debug.hpp"

#include <algorithm>
#include <format>
#include <sstream>

namespace assets {

Material::Material(toml::table table) : m_table(std::move(table)) {
	const auto* params = m_table.get_as<toml::table>("params");
	if (!params) {
		return;
	}

	const auto* albedo = params->get_as<std::string>("albedo_map");
	if (!albedo || albedo->get().empty()) {
		return;
	}

	const std::string& ref = albedo->get();
	if (ref.size() == 11) {
		m_albedo_uid = toast::UID(toast::UID::fromString(ref));
		return;
	}

	if (const auto uid = assets::resolveURI(ref)) {
		m_albedo_uid = *uid;
	}
}

auto Material::get() const noexcept -> const toml::table& {
	return m_table;
}

auto Material::get() noexcept -> toml::table& {
	return m_table;
}

auto Material::serialize(SaveMode mode) const -> std::vector<uint8_t> {
	(void)mode;

	std::ostringstream ss;
	ss << m_table;
	const std::string text = ss.str();
	return {text.begin(), text.end()};
}

void Material::resolveTextureHandles() {
	if (m_albedo_map.hasValue() || m_albedo_uid.data() == 0) {
		return;
	}

	m_albedo_map = assets::load<Texture>(m_albedo_uid);

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
