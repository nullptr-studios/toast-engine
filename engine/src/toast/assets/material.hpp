/**
 * @file material.hpp
 * @author Xein
 * @date 12 Jun 2026
 *
 * @brief Material asset backed by TOML data
 */

#pragma once
#include "core_types.hpp"
#include "data.hpp"

#include <vulkan/vulkan_raii.hpp>

namespace assets {
class Texture;

class TOAST_API Material : public Data {
public:
	static constexpr std::string_view collection = "materials";

	explicit Material(const toml::table& table, AssetHandle<Schema> schema = {});

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "material";
	}

	[[nodiscard]]
	auto serialize(SaveMode mode) const -> std::vector<uint8_t> override;

	[[nodiscard]]
	auto albedoMap() const -> const AssetHandle<Texture>& {
		return m_albedo_map;
	}

	[[nodiscard]]
	auto albedoMap() -> AssetHandle<Texture>& {
		return m_albedo_map;
	}

	[[nodiscard]]
	auto normalMap() const -> const AssetHandle<Texture>& {
		return m_normal_map;
	}

	[[nodiscard]]
	auto normalMap() -> AssetHandle<Texture>& {
		return m_normal_map;
	}

	[[nodiscard]]
	auto color() const noexcept -> const glm::vec4& {
		return m_color;
	}

	vk::raii::Sampler& albedoSampler() { return m_albedoSampler; }

	void resolveTextureHandles();

private:
	AssetHandle<Texture> m_albedo_map;
	AssetHandle<Texture> m_normal_map;
	glm::vec4 m_color = glm::vec4(1.0f);

	vk::raii::Sampler m_albedoSampler = nullptr;    // THISSHOULDBECREATEDPERIMAGESAMPLER
};
}
