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
#include "shader.hpp"

#include <memory>
#include <vulkan/vulkan_core.h>

namespace renderer {
class VulkanSampler;
}

namespace assets {
class Texture;

enum class BlendMode : uint8_t {
	opaque,
	alpha,
	additive,
	multiply,
};

enum class CullMode : uint8_t {
	none,
	front,
	back,
};

/**
 * @struct MaterialSettings
 * @brief Fixed-function pipeline state carried by the material's [settings] block
 */
struct MaterialSettings {
	BlendMode blend_mode = BlendMode::opaque;
	bool depth_test = true;
	bool depth_write = true;
	CullMode cull_mode = CullMode::back;
};

/**
 * @class Material
 * @brief Shader-driven material asset
 *
 * The @c shaders array defines which shader assets the material renders with; every
 * other key holds a value for a shader parameter discovered via reflection
 *
 * The trailing @c settings table carries fixed-function state
 */
class TOAST_API Material : public Data {
public:
	explicit Material(const toml::table& table, AssetHandle<Schema> schema = {});
	~Material() override;

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "material";
	}

	[[nodiscard]]
	auto serialize(SaveMode mode) const -> std::vector<uint8_t> override;

	void reload(const toml::table& table) override;

	[[nodiscard]]
	auto name() const -> std::string;    ///< @brief Display name of the material

	[[nodiscard]]
	virtual auto shaders() const -> const std::vector<AssetHandle<Shader>>&;

	[[nodiscard]]
	virtual auto settings() const -> MaterialSettings;

	/**
	 * @brief Looks up a shader parameter value by name
	 */
	[[nodiscard]]
	virtual auto value(std::string_view key) const -> const DataValue*;

	/**
	 * @brief The material owning the render pass, instances return their parent
	 */
	[[nodiscard]]
	virtual auto rootMaterial() -> Material* {
		return this;
	}

	// Legacy parameter API

	[[nodiscard]]
	auto albedoMap() const -> AssetHandle<Texture>;

	[[nodiscard]]
	auto normalMap() const -> AssetHandle<Texture>;

	[[nodiscard]]
	auto color() const -> glm::vec4;

	[[nodiscard]]
	auto albedoSampler() const -> VkSampler;

	void resolveTextureHandles();

private:
	mutable std::vector<AssetHandle<Shader>> m_shader_handles;
	mutable bool m_shaders_dirty = true;

	// Legacy members
	mutable AssetHandle<Texture> m_albedo_handle;
	mutable AssetHandle<Texture> m_normal_handle;
	bool m_sampler_ready = false;
	std::unique_ptr<renderer::VulkanSampler> m_albedo_sampler;
};
}
