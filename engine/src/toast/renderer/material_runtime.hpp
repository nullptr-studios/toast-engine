/**
 * @file material_runtime.hpp
 * @author Xein
 * @date 17 Jul 2026
 */

#pragma once

#include "shader_cache.hpp"
#include "vulkan_sampler.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <toast/assets/material.hpp>
#include <toast/assets/texture.hpp>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

namespace renderer {
class VulkanCore;

class MaterialRuntime {
public:
	MaterialRuntime(const VulkanCore& core, assets::Material* material);

	[[nodiscard]]
	auto material() const -> assets::Material* {
		return m_material;
	}

	[[nodiscard]]
	auto reflection() const -> const ShaderReflection& {
		return m_merged;
	}

	[[nodiscard]]
	auto shaderEntries() const -> const std::vector<std::shared_ptr<const ShaderCache::Entry>>& {
		return m_entries;
	}

	void rebuild();

	void markValuesDirty() { m_values_dirty = true; }

	struct UboBlob {
		uint32_t set = 0;
		uint32_t binding = 0;
		std::vector<std::byte> bytes;
	};

	auto uniformBlobs() -> const std::vector<UboBlob>&;

	auto pushBlob() -> const std::vector<std::byte>&;

	[[nodiscard]]
	auto modelOffset() const -> std::optional<uint32_t> {
		return m_model_offset;
	}

	[[nodiscard]]
	auto jointOffsetOffset() const -> std::optional<uint32_t> {
		return m_joint_offset_offset;
	}

	[[nodiscard]]
	auto instanceBaseOffset() const -> std::optional<uint32_t> {
		return m_instance_base_offset;
	}

	struct TextureSlot {
		uint32_t set = 0;
		uint32_t binding = 0;
		assets::Handle<assets::Texture> texture;
		vk::Sampler sampler;
		std::string default_fallback;
		bool linear_data = false;
	};

	auto textureSlots() -> const std::vector<TextureSlot>&;

private:
	void bakeValues();
	void bakeTextures();

	[[nodiscard]]
	auto groupTomlKey(const std::string& group) const -> std::string;

	[[nodiscard]]
	auto resolveMemberValue(const ShaderBlockMember& member) const -> const assets::DataValue*;
	auto samplerFor(const assets::DataValue* params, std::string_view debug_name) -> vk::Sampler;

	const VulkanCore* m_core = nullptr;
	assets::Material* m_material = nullptr;

	assets::Handle<assets::Material> m_material_ref;

	ShaderReflection m_merged;
	std::vector<std::shared_ptr<const ShaderCache::Entry>> m_entries;

	std::vector<UboBlob> m_ubo_blobs;
	std::vector<std::byte> m_push_blob;
	std::optional<uint32_t> m_model_offset;
	std::optional<uint32_t> m_joint_offset_offset;
	std::optional<uint32_t> m_instance_base_offset;
	std::vector<TextureSlot> m_texture_slots;
	bool m_values_dirty = true;
	bool m_textures_dirty = true;

	std::unordered_map<uint64_t, std::unique_ptr<VulkanSampler>> m_samplers;
};

}
