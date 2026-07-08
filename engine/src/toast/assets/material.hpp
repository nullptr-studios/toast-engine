/**
 * @file material.hpp
 * @author Xein
 * @date 12 Jun 2026
 *
 * @brief Material asset backed by TOML data
 */

#pragma once
#include "core_types.hpp"

#include <vulkan/vulkan_raii.hpp>

namespace assets {
class Texture;

/**
 * @brief Asset representing parsed material TOML data (.tmat)
 */
class TOAST_API Material : public Asset, public ISaveable {
public:
	explicit Material(toml::table table);

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "material";
	}

	[[nodiscard]]
	auto get() const noexcept -> const toml::table&;

	[[nodiscard]]
	auto get() noexcept -> toml::table&;

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

	vk::raii::Sampler& albedoSampler() { return m_albedoSampler; }

	void resolveTextureHandles();

private:
	toml::table m_table;

	AssetHandle<Texture> m_albedo_map;
	vk::raii::Sampler m_albedoSampler = nullptr;    // THISSHOULDBECREATEDPERIMAGESAMPLER
	toast::UID m_albedo_uid {};
};
}
