#include "material.hpp"

#include "assets.hpp"
#include "texture.hpp"
#include "toast/renderer/vulkan_debug.hpp"
#include "toast/renderer/vulkan_renderer.hpp"

#include <algorithm>
#include <format>

namespace assets {

namespace {

// Material is built with Data::keep_all_keys, which intentionally skips schema-guided parsing (see
// Data::buildRoot) so it round-trips arbitrary keys. That means fields never get typed as asset_t/node_t
// or color4_t the way a schema-guided Data would type them - they come in as whatever the raw TOML literal
// naturally maps to (a UID is just a string_t, a color is just an array_t of numbers). Read them as such
// here instead of static_cast'ing to the typed accessor, which asserts/throws on the type mismatch.

auto readAssetUID(const DataValue& v) -> toast::UID {
	if (v.type() == DataType::asset_t || v.type() == DataType::node_t) {
		return static_cast<toast::UID>(v);
	}
	if (v.type() == DataType::string_t) {
		const auto s = static_cast<std::string>(v);
		if (s.size() == 11) {
			return {toast::UID::fromString(s)};
		}
	}
	return toast::UID(uint64_t {0});
}

auto readColor(const DataValue& v) -> glm::vec4 {
	if (v.type() == DataType::color4_t) {
		return static_cast<glm::vec4>(v);
	}
	if (!v.isArray()) {
		return glm::vec4(1.0f);
	}
	glm::vec4 result(1.0f);
	const size_t count = std::min<size_t>(v.size(), 4);
	for (size_t i = 0; i < count; ++i) {
		const DataValue& elem = v[i];
		if (auto d = elem.value<double>()) {
			result[static_cast<int>(i)] = static_cast<float>(*d);
		} else if (auto n = elem.value<int64_t>()) {
			result[static_cast<int>(i)] = static_cast<float>(*n);
		}
	}
	return result;
}

}

Material::Material(const toml::table& table, AssetHandle<Schema> schema) : Data(table, std::move(schema), Data::keep_all_keys) { }

auto Material::serialize(SaveMode mode) const -> std::vector<uint8_t> {
	return Data::serialize(mode);
}

auto Material::albedoMap() const -> AssetHandle<Texture> {
	if (!m_root.contains("albedo_map")) {
		m_albedo_handle = {};
		return m_albedo_handle;
	}
	const toast::UID uid = readAssetUID(m_root["albedo_map"]);
	if (uid.data() == 0) {
		m_albedo_handle = {};
		return m_albedo_handle;
	}
	if (m_albedo_handle.uid().data() != uid.data()) {
		m_albedo_handle = assets::load<Texture>(uid);
	}
	return m_albedo_handle;
}

auto Material::normalMap() const -> AssetHandle<Texture> {
	if (!m_root.contains("normal_map")) {
		m_normal_handle = {};
		return m_normal_handle;
	}
	const toast::UID uid = readAssetUID(m_root["normal_map"]);
	if (uid.data() == 0) {
		m_normal_handle = {};
		return m_normal_handle;
	}
	if (m_normal_handle.uid().data() != uid.data()) {
		m_normal_handle = assets::load<Texture>(uid);
	}
	return m_normal_handle;
}

auto Material::color() const -> glm::vec4 {
	if (!m_root.contains("color")) {
		return glm::vec4(1.0f);
	}
	return readColor(m_root["color"]);
}

void Material::resolveTextureHandles() {
	const AssetHandle<Texture> albedo_map = albedoMap();
	if (!albedo_map.hasValue()) {
		return;
	}

	// This is called every frame
	if (m_sampler_ready) {
		return;
	}

	const auto& core = toast::renderer::getCore();

	vk::SamplerCreateInfo sampler_info {};

	sampler_info.magFilter = vk::Filter::eLinear;
	sampler_info.minFilter = vk::Filter::eLinear;
	sampler_info.addressModeU = vk::SamplerAddressMode::eRepeat;
	sampler_info.addressModeV = vk::SamplerAddressMode::eRepeat;
	sampler_info.addressModeW = vk::SamplerAddressMode::eRepeat;
	sampler_info.anisotropyEnable = core.supportsSamplerAnisotropy() ? vk::True : vk::False;
	sampler_info.maxAnisotropy = core.supportsSamplerAnisotropy() ? std::min(16.0f, core.maxSamplerAnisotropy()) : 1.0f;
	sampler_info.compareEnable = vk::False;
	sampler_info.compareOp = vk::CompareOp::eAlways;
	sampler_info.unnormalizedCoordinates = vk::False;

	sampler_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
	sampler_info.mipLodBias = 0.0f;
	sampler_info.minLod = 0.0f;
	sampler_info.maxLod = 0.0f;

	m_albedo_sampler = vk::raii::Sampler(core.getDevice(), sampler_info);
	toast::renderer::setDebugName(core, *m_albedo_sampler, std::format("Material AlbedoSampler ({})", albedo_map.path()));

	m_sampler_ready = true;
}

}
