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

// FIXME: This should be changed and improved, this doess not currently support material reloading
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
	auto albedoMap() const -> AssetHandle<Texture>;

	[[nodiscard]]
	auto normalMap() const -> AssetHandle<Texture>;

	[[nodiscard]]
	auto color() const -> glm::vec4;

	auto albedoSampler() -> vk::raii::Sampler& { return m_albedo_sampler; }

	void resolveTextureHandles();

private:
	mutable AssetHandle<Texture> m_albedo_handle;
	mutable AssetHandle<Texture> m_normal_handle;
	bool m_sampler_ready = false;

	vk::raii::Sampler m_albedo_sampler = nullptr;    // THISSHOULDBECREATEDPERIMAGESAMPLER
};
}
