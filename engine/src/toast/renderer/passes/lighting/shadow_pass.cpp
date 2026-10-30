/**
 * @file shadow_pass.cpp
 * @author dario
 * @date 01/08/2026
 */

#include "toast/renderer/passes/lighting/shadow_pass.hpp"

#include "toast/renderer/core/descriptor_writer.hpp"
#include "toast/renderer/core/vulkan_core.hpp"
#include "toast/renderer/core/vulkan_debug.hpp"
#include "toast/renderer/lighting/frustum.hpp"
#include "toast/renderer/resources/vulkan_mesh.hpp"
#include "toast/renderer/shaders/shader_cache.hpp"
#include "toast/renderer/voxel/voxel_gpu_storage.hpp"
#include "toast/renderer/vulkan_renderer.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <toast/voxel/gpu_layout.hpp>
#include <toast/voxel/voxel_constants.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

namespace {

/// A moving volume would invalidate every resting voxel caster each frame so it draws with the meshes instead
/// Frames a caster stays moving after its last step
constexpr uint64_t k_caster_settle_frames = 30;

auto cornerDrift(const glm::mat4& from, const glm::mat4& to, const glm::vec3& extent) -> float {
	const glm::mat4 delta = to - from;
	float drift = 0.0f;
	for (uint32_t corner = 0; corner < 8; ++corner) {
		const glm::vec4 local(
		    (corner & 1u) != 0 ? extent.x : 0.0f, (corner & 2u) != 0 ? extent.y : 0.0f, (corner & 4u) != 0 ? extent.z : 0.0f, 1.0f
		);
		drift = std::max(drift, glm::length(glm::vec3(delta * local)));
	}
	return drift;
}

/// Mirrors kTaskGroupsPerCell in voxel_dda.slang
constexpr uint32_t k_task_groups_per_cell = 2;

/// Shared by every caster pipeline and mirrored by kSlopeBias in voxel_shadow.slang
constexpr float k_caster_bias_constant = 1.5f;
constexpr float k_caster_bias_slope = 1.25f;

auto shadowLayerRange(uint32_t base_layer, uint32_t layer_count) -> vk::ImageSubresourceRange {
	return {vk::ImageAspectFlagBits::eDepth, 0, 1, base_layer, layer_count};
}

struct ShadowSignature {
	uint64_t value = 0x243F6A8885A308D3ull;

	void add(uint64_t word) noexcept {
		value = (value ^ word) * 0x9E3779B97F4A7C15ull;
		value ^= value >> 29;
	}

	void add(const glm::mat4& matrix) noexcept {
		for (int column = 0; column < 4; ++column) {
			for (int row = 0; row < 4; row += 2) {
				uint32_t high = 0;
				uint32_t low = 0;
				std::memcpy(&high, &matrix[column][row], sizeof(high));
				std::memcpy(&low, &matrix[column][row + 1], sizeof(low));
				add((static_cast<uint64_t>(high) << 32) | low);
			}
		}
	}
};

}

auto createShadowSampler(const VulkanCore& core, vk::Format format) -> vk::raii::Sampler {
	const auto props = core.getPhysicalDevice().getFormatProperties(format);
	const bool linear_filterable =
	    (props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear) != vk::FormatFeatureFlags {};

	const vk::Filter filter = linear_filterable ? vk::Filter::eLinear : vk::Filter::eNearest;

	vk::SamplerCreateInfo sampler_ci {};
	sampler_ci.magFilter = filter;
	sampler_ci.minFilter = filter;
	sampler_ci.addressModeU = vk::SamplerAddressMode::eClampToBorder;
	sampler_ci.addressModeV = vk::SamplerAddressMode::eClampToBorder;
	sampler_ci.addressModeW = vk::SamplerAddressMode::eClampToBorder;
	sampler_ci.borderColor = vk::BorderColor::eFloatOpaqueWhite;
	sampler_ci.mipmapMode = vk::SamplerMipmapMode::eNearest;
	sampler_ci.compareEnable = VK_TRUE;
	sampler_ci.compareOp = vk::CompareOp::eLessOrEqual;

	return {core.getDevice(), sampler_ci};
}

auto ShadowPass::selectShadowFormat(const VulkanCore& core) -> vk::Format {
	const std::array candidates {vk::Format::eD32Sfloat, vk::Format::eD16Unorm};

	constexpr auto required = vk::FormatFeatureFlagBits::eDepthStencilAttachment | vk::FormatFeatureFlagBits::eSampledImage;

	for (const auto candidate : candidates) {
		const auto props = core.getPhysicalDevice().getFormatProperties(candidate);
		if ((props.optimalTilingFeatures & required) == required) {
			return candidate;
		}
	}

	return vk::Format::eUndefined;
}

ShadowPass::ShadowPass(const VulkanCore& core) : m_core(&core) {
	ZoneScoped;
	m_format = selectShadowFormat(core);
	if (m_format == vk::Format::eUndefined) {
		TOAST_ERROR("Render", "No sampleable depth format available, shadows are disabled");
		return;
	}

	const auto uid = assets::resolveURI("core://shaders/shadow_depth.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "ShadowPass shader core://shaders/shadow_depth.slang unavailable, shadows will not render");
		return;
	}

	m_shader_layout.rebuild(core, shader->reflection, "ShadowPass");

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.debug_name = "ShadowPass";
	config.depth_only = true;
	config.depth_format = m_format;
	config.extent = vk::Extent2D {shadows::cascadeResolution(), shadows::cascadeResolution()};
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_shader_layout.getPipelineLayout();
	config.vertex_bindings = {vertexBindingDescription()};
	config.vertex_attributes = {vertexAttributeDescriptions()[0]};
	config.depth_test = true;
	config.depth_write = true;
	// Shadow maps keep standard depth whatever the scene convention
	config.depth_compare = vk::CompareOp::eLess;
	config.depth_bias_constant = k_caster_bias_constant;
	config.depth_bias_slope = k_caster_bias_slope;
	config.depth_clamp = core.isDepthClampSupported();
	config.cull_mode = vk::CullModeFlagBits::eNone;

	// Single view is 0x1 not 0 since 0 disables multiview and leaves SV_ViewID undefined
	constexpr std::array view_masks {
	  1u,
	  (1u << shadows::k_cube_faces) - 1u,
	};

	static_assert(view_masks.size() == std::tuple_size_v<decltype(m_pipeline_sets)>, "one pipeline set per mask");

	for (size_t mask_index = 0; mask_index < view_masks.size(); ++mask_index) {
		const uint32_t view_mask = view_masks[mask_index];

		// VulkanPipeline is neither copyable nor movable
		PipelineSet& set = m_pipeline_sets[mask_index];
		set.view_mask = view_mask;

		VulkanPipeline::Config mask_config = config;
		mask_config.view_mask = view_mask;
		mask_config.debug_name = std::format("ShadowPass [viewMask {:#x}]", view_mask);
		set.pipeline.rebuild(core, mask_config);
	}

	createResources(core);
	createVoxelResources(core);

	TOAST_INFO(
	    "Render",
	    "ShadowPass ready: {} cascades at {}x{}, {} punctual layers at {}x{} ({})",
	    shadows::k_cascade_count,
	    shadows::cascadeResolution(),
	    shadows::cascadeResolution(),
	    shadows::punctualLayerCount(),
	    shadows::punctualResolution(),
	    shadows::punctualResolution(),
	    vk::to_string(m_format)
	);
}

auto ShadowPass::pipelineSetFor(uint32_t view_mask) const -> const PipelineSet* {
	for (const PipelineSet& set : m_pipeline_sets) {
		if (set.view_mask == view_mask) {
			return &set;
		}
	}
	return nullptr;
}

void ShadowPass::createShadowMap(
    const VulkanCore& core, ShadowMap& map, uint32_t resolution, std::span<const uint32_t> group_layers,
    vk::ImageUsageFlags usage, std::string_view debug_name
) {
	ZoneScoped;
	const auto& device = core.getDevice();

	uint32_t layer_count = 0;
	for (const uint32_t count : group_layers) {
		layer_count += count;
	}

	map.resolution = resolution;
	map.layer_count = layer_count;

	vk::ImageCreateInfo image_ci {};
	image_ci.imageType = vk::ImageType::e2D;
	image_ci.format = m_format;
	image_ci.extent = vk::Extent3D {resolution, resolution, 1};
	image_ci.mipLevels = 1;
	image_ci.arrayLayers = layer_count;
	image_ci.samples = vk::SampleCountFlagBits::e1;
	image_ci.tiling = vk::ImageTiling::eOptimal;
	image_ci.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | usage;
	image_ci.sharingMode = vk::SharingMode::eExclusive;
	image_ci.initialLayout = vk::ImageLayout::eUndefined;

	vma::AllocationCreateInfo allocation_ci {};
	allocation_ci.usage = vma::MemoryUsage::eAutoPreferDevice;

	map.image.emplace(core.getAllocator().createImage(image_ci, allocation_ci));
	setDebugName(core, **map.image, std::string(debug_name));
	map.state.reset(**map.image, shadowLayerRange(0, layer_count));

	vk::ImageViewCreateInfo array_view_ci {};
	array_view_ci.image = **map.image;
	array_view_ci.viewType = vk::ImageViewType::e2DArray;
	array_view_ci.format = m_format;
	array_view_ci.subresourceRange = shadowLayerRange(0, layer_count);
	map.array_view.emplace(device, array_view_ci);
	setDebugName(core, **map.array_view, std::format("{} ArrayView", debug_name));

	// SV_ViewID is relative to the view base layer so group layers must be consecutive
	map.groups.clear();
	map.groups.reserve(group_layers.size());

	uint32_t base_layer = 0;
	for (const uint32_t count : group_layers) {
		LayerGroup group;
		group.base_layer = base_layer;
		group.layer_count = count;
		group.view_mask = (1u << count) - 1u;
		group.dirty = true;

		vk::ImageViewCreateInfo view_ci {};
		view_ci.image = **map.image;
		view_ci.viewType = vk::ImageViewType::e2DArray;
		view_ci.format = m_format;
		view_ci.subresourceRange = shadowLayerRange(base_layer, count);
		group.view.emplace(device, view_ci);
		setDebugName(core, **group.view, std::format("{} GroupView[{}..{}]", debug_name, base_layer, base_layer + count - 1));

		map.groups.push_back(std::move(group));
		base_layer += count;
	}
}

void ShadowPass::createResources(const VulkanCore& core) {
	ZoneScoped;
	const auto& device = core.getDevice();
	const auto& layouts = m_shader_layout.getDescriptorSetLayouts();
	if (layouts.empty()) {
		TOAST_ERROR("Render", "ShadowPass shader layout has no descriptor sets");
		return;
	}

	m_sampler = createShadowSampler(core, m_format);
	setDebugName(core, *m_sampler, "ShadowPass Sampler");

	const vk::DescriptorSetLayout set_layout = *layouts[0];
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();

	const auto uid = assets::resolveURI("core://shaders/shadow_depth.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "ShadowPass shader disappeared between pipeline creation and resource setup");
		return;
	}

	m_targets.resize(VulkanRenderer::k_frames_in_flight);
	m_descriptor_sets.clear();
	m_descriptor_sets.reserve(VulkanRenderer::k_frames_in_flight);

	// One group per cascade so a refit of one cascade leaves the cached others alone
	const std::vector<uint32_t> cascade_groups(shadows::k_cascade_count, 1u);
	std::vector<uint32_t> punctual_groups(shadows::spotShadowSlots(), 1u);
	punctual_groups.insert(punctual_groups.end(), shadows::pointShadowSlots(), shadows::k_cube_faces);
	if (punctual_groups.empty()) {
		// A layer nothing renders into so the map and its descriptors still exist with every slot off
		punctual_groups.push_back(1u);
	}

	constexpr auto k_static_usage = vk::ImageUsageFlagBits::eTransferSrc;
	createShadowMap(
	    core, m_static.cascades, shadows::cascadeResolution(), cascade_groups, k_static_usage, "ShadowPass StaticCascades"
	);
	createShadowMap(
	    core, m_static.punctual, shadows::punctualResolution(), punctual_groups, k_static_usage, "ShadowPass StaticPunctual"
	);

	constexpr auto k_slot_usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		auto& target = m_targets[i];

		createShadowMap(
		    core,
		    target.cascades,
		    shadows::cascadeResolution(),
		    cascade_groups,
		    k_slot_usage,
		    std::format("ShadowPass Cascades[{}]", i)
		);
		createShadowMap(
		    core,
		    target.punctual,
		    shadows::punctualResolution(),
		    punctual_groups,
		    k_slot_usage,
		    std::format("ShadowPass Punctual[{}]", i)
		);

		vk::BufferCreateInfo buffer_ci {};
		buffer_ci.size = sizeof(ShadowUBO);
		buffer_ci.usage = vk::BufferUsageFlagBits::eUniformBuffer;

		vma::AllocationCreateInfo buffer_alloc_ci {};
		buffer_alloc_ci.usage = vma::MemoryUsage::eAutoPreferHost;
		buffer_alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

		target.ubo.gpu_buffer.emplace(core.getAllocator().createBuffer(buffer_ci, buffer_alloc_ci));
		setDebugName(core, **target.ubo.gpu_buffer, std::format("ShadowPass ShadowUBO[{}]", i));

		const vk::DescriptorSetAllocateInfo alloc_info(pool, 1, &set_layout);
		auto allocated = device.allocateDescriptorSets(alloc_info);
		m_descriptor_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_descriptor_sets[i], std::format("ShadowPass DescriptorSet[{}]", i));

		DescriptorWriter writer;

		for (const auto& binding : shader->reflection.bindings) {
			if (binding.set != 0) {
				continue;
			}
			if (binding.name == "gShadow") {
				writer.buffer(
				    *m_descriptor_sets[i], binding.binding, vk::DescriptorType::eUniformBuffer, **target.ubo.gpu_buffer, sizeof(ShadowUBO)
				);
			} else if (binding.name == "gInstances") {
				writer.buffer(
				    *m_descriptor_sets[i],
				    binding.binding,
				    vk::DescriptorType::eStorageBuffer,
				    VulkanRenderer::instance->getShadowInstanceBuffer(i),
				    sizeof(VulkanRenderer::InstanceData) * VulkanRenderer::k_max_instances
				);
			}
		}

		writer.flush(device);
	}
}

void ShadowPass::createVoxelResources(const VulkanCore& core) {
	ZoneScoped;
	static_assert(sizeof(VoxelShadowPush) == 16, "VoxelShadowPush is mirrored by voxel_shadow.slang");

	const auto uid = assets::resolveURI("core://shaders/voxel_shadow.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_WARN("Render", "ShadowPass voxel shader core://shaders/voxel_shadow.slang unavailable, voxels will not cast shadows");
		return;
	}

	m_voxel_layout.rebuild(core, shader->reflection, "ShadowPass Voxels");
	const auto& layouts = m_voxel_layout.getDescriptorSetLayouts();
	if (layouts.size() < 2 || m_targets.size() < VulkanRenderer::k_frames_in_flight) {
		TOAST_ERROR("Render", "ShadowPass voxel shader needs the shadow set and the voxel storage set, voxels will not cast shadows");
		return;
	}

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.depth_only = true;
	config.depth_fragment = true;
	config.depth_format = m_format;
	config.extent = vk::Extent2D {shadows::cascadeResolution(), shadows::cascadeResolution()};
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_voxel_layout.getPipelineLayout();
	config.vertex_bindings = {};
	config.vertex_attributes = {};
	config.topology = vk::PrimitiveTopology::eTriangleList;
	config.depth_test = true;
	config.depth_write = true;
	// Shadow maps keep standard depth whatever the scene convention
	config.depth_compare = vk::CompareOp::eLess;
	config.depth_clamp = core.isDepthClampSupported();
	// Shadow projections are not Y flipped
	config.front_face = vk::FrontFace::eClockwise;

	for (size_t mask_index = 0; mask_index < m_voxel_pipeline_sets.size(); ++mask_index) {
		VoxelPipelineSet& set = m_voxel_pipeline_sets[mask_index];
		set.view_mask = m_pipeline_sets[mask_index].view_mask;
		for (const bool cull_front : {false, true}) {
			for (const bool exact : {false, true}) {
				VulkanPipeline::Config variant = config;
				variant.view_mask = set.view_mask;
				variant.cull_mode = cull_front ? vk::CullModeFlagBits::eFront : vk::CullModeFlagBits::eBack;
				variant.fragment_entry = exact ? "fragmentInside" : "fragmentOutside";
				variant.debug_name = std::format(
				    "ShadowPass Voxels [viewMask {:#x}] {} {}",
				    set.view_mask,
				    cull_front ? "CullFront" : "CullBack",
				    exact ? "Inside" : "Outside"
				);
				set.pipelines[cull_front ? 1 : 0][exact ? 1 : 0].rebuild(core, variant);
			}
		}
	}

	const auto& device = core.getDevice();
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();
	const vk::DescriptorSetLayout shadow_layout = *layouts[0];
	const vk::DescriptorSetLayout storage_layout = *layouts[1];

	m_voxel_shadow_sets.clear();
	m_voxel_storage_sets.clear();
	m_voxel_instance_buffers.clear();
	m_voxel_bound_storage.assign(VulkanRenderer::k_frames_in_flight, nullptr);

	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		vk::BufferCreateInfo buffer_ci {};
		buffer_ci.size = sizeof(VoxelInstanceGpu) * k_max_voxel_casters;
		buffer_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer;

		vma::AllocationCreateInfo alloc_ci {};
		alloc_ci.usage = vma::MemoryUsage::eAutoPreferHost;
		alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

		m_voxel_instance_buffers.push_back(core.getAllocator().createBuffer(buffer_ci, alloc_ci));
		setDebugName(core, *m_voxel_instance_buffers.back(), std::format("ShadowPass VoxelInstances[{}]", i));

		auto shadow_set = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &shadow_layout));
		m_voxel_shadow_sets.push_back(std::move(shadow_set[0]));
		setDebugName(core, *m_voxel_shadow_sets.back(), std::format("ShadowPass VoxelShadowSet[{}]", i));

		auto storage_set = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &storage_layout));
		m_voxel_storage_sets.push_back(std::move(storage_set[0]));
		setDebugName(core, *m_voxel_storage_sets.back(), std::format("ShadowPass VoxelStorageSet[{}]", i));

		DescriptorWriter writer;
		for (const auto& binding : shader->reflection.bindings) {
			if (binding.set == 0 && binding.name == "gShadow" && m_targets[i].ubo.gpu_buffer.has_value()) {
				writer.buffer(
				    *m_voxel_shadow_sets[i],
				    binding.binding,
				    vk::DescriptorType::eUniformBuffer,
				    **m_targets[i].ubo.gpu_buffer,
				    sizeof(ShadowUBO)
				);
			} else if (binding.set == 1 && binding.name == "gVoxelInstances") {
				writer.buffer(
				    *m_voxel_storage_sets[i],
				    binding.binding,
				    vk::DescriptorType::eStorageBuffer,
				    *m_voxel_instance_buffers[i],
				    sizeof(VoxelInstanceGpu) * k_max_voxel_casters
				);
			}
		}
		writer.flush(device);
	}

	// Matches depth_bias_constant in units of the smallest depth step of each format
	m_voxel_constant_bias = m_format == vk::Format::eD16Unorm ? 1.5f / 65535.0f : std::ldexp(1.5f, -24);

	m_voxels_ready = std::ranges::all_of(m_voxel_pipeline_sets, [](const VoxelPipelineSet& set) {
		return std::ranges::all_of(set.pipelines, [](const auto& by_depth) {
			return std::ranges::all_of(by_depth, [](const VulkanPipeline& pipeline) { return pipeline.isReady(); });
		});
	});
	if (!m_voxels_ready) {
		TOAST_ERROR("Render", "ShadowPass voxel pipelines failed to build, voxels will not cast shadows");
		return;
	}
	createVoxelMeshResources(core);
}

void ShadowPass::createVoxelMeshResources(const VulkanCore& core) {
	ZoneScoped;
	static_assert(sizeof(VoxelMeshShadowPush) == 16, "VoxelMeshShadowPush is mirrored by voxel_shadow_mesh_common.slang");
	if (core.meshMultiviewViews() == 0) {
		TOAST_INFO("Render", "ShadowPass voxels march since multiview mesh shaders are unavailable");
		return;
	}

	const auto mesh_uid = assets::resolveURI("core://shaders/voxel_shadow_mesh.slang");
	const auto task_uid = assets::resolveURI("core://shaders/voxel_shadow_task.slang");
	const auto mesh_shader = mesh_uid.has_value() ? ShaderCache::get().acquire(*mesh_uid) : nullptr;
	const auto task_shader = task_uid.has_value() ? ShaderCache::get().acquire(*task_uid) : nullptr;
	if (!mesh_shader || !task_shader) {
		TOAST_WARN(
		    "Render",
		    "ShadowPass shaders core://shaders/voxel_shadow_mesh.slang and voxel_shadow_task.slang unavailable, voxels march"
		);
		return;
	}

	// Same sets as voxel_shadow.slang so the voxel shadow and storage sets bind to either layout
	m_voxel_mesh_layout.rebuild(core, mesh_shader->reflection, "ShadowPass VoxelMesh");

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.depth_only = true;
	config.depth_format = m_format;
	config.extent = vk::Extent2D {shadows::cascadeResolution(), shadows::cascadeResolution()};
	config.shader_spirv = mesh_shader->spirv;
	config.task_spirv = task_shader->spirv;
	config.task_entry = "shadowTaskMain";
	config.mesh_entry = "shadowMeshMain";
	config.pipeline_layout = *m_voxel_mesh_layout.getPipelineLayout();
	config.depth_test = true;
	config.depth_write = true;
	config.depth_compare = vk::CompareOp::eLess;
	config.depth_bias_constant = k_caster_bias_constant;
	config.depth_bias_slope = k_caster_bias_slope;
	config.depth_clamp = core.isDepthClampSupported();
	config.cull_mode = vk::CullModeFlagBits::eNone;

	for (size_t mask_index = 0; mask_index < m_voxel_mesh_pipelines.size(); ++mask_index) {
		VulkanPipeline::Config variant = config;
		variant.view_mask = m_voxel_pipeline_sets[mask_index].view_mask;
		// Groups wider than the device allows march
		if (static_cast<uint32_t>(std::popcount(variant.view_mask)) > core.meshMultiviewViews()) {
			continue;
		}
		variant.debug_name = std::format("ShadowPass VoxelMesh [viewMask {:#x}]", variant.view_mask);
		m_voxel_mesh_pipelines[mask_index].rebuild(core, variant);
	}

	const auto properties =
	    core.getPhysicalDevice().getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceMeshShaderPropertiesEXT>();
	const auto& mesh_properties = properties.get<vk::PhysicalDeviceMeshShaderPropertiesEXT>();
	std::ranges::copy(mesh_properties.maxTaskWorkGroupCount, m_max_task_groups.begin());
	m_max_task_group_total = mesh_properties.maxTaskWorkGroupTotalCount;

	// Extension entry points are not exported by the loader library so they come from the device dispatcher
	m_draw_mesh_tasks = core.getDevice().getDispatcher()->vkCmdDrawMeshTasksEXT;
	TOAST_INFO(
	    "Render",
	    "ShadowPass voxels near enough to the light rasterize task and mesh shader quads in groups of up to {} views",
	    core.meshMultiviewViews()
	);
}

auto ShadowPass::voxelMeshPipelineFor(uint32_t view_mask) const -> const VulkanPipeline* {
	for (size_t mask_index = 0; mask_index < m_voxel_mesh_pipelines.size(); ++mask_index) {
		if (m_voxel_pipeline_sets[mask_index].view_mask == view_mask && m_voxel_mesh_pipelines[mask_index].isReady()) {
			return &m_voxel_mesh_pipelines[mask_index];
		}
	}
	return nullptr;
}

auto ShadowPass::prepareVoxels(uint32_t frame_index) -> uint32_t {
	ZoneScoped;
	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (!m_voxels_ready || frame == nullptr || frame_index >= m_voxel_instance_buffers.size()) {
		return 0;
	}

	const auto& storage = frame->voxel_storage;
	if (frame->voxel_instances.empty() || !storage || !storage->isReady()) {
		return 0;
	}

	if (m_voxel_bound_storage[frame_index] != storage) {
		// Safe since the fence of this slot was waited on
		writeVoxelStorageDescriptors(m_core->getDevice(), *m_voxel_storage_sets[frame_index], *storage);
		m_voxel_bound_storage[frame_index] = storage;
	}

	const auto& allocation = m_voxel_instance_buffers[frame_index].getAllocation();
	auto* instances = static_cast<VoxelInstanceGpu*>(allocation.getInfo().pMappedData);
	if (instances == nullptr) {
		return 0;
	}

	// Every proxy not only camera visible ones since an off screen volume still casts into view
	const auto count = static_cast<uint32_t>(std::min<size_t>(frame->voxel_instances.size(), k_max_voxel_casters));
	if (frame->voxel_instances.size() > k_max_voxel_casters) {
		const auto now = std::chrono::steady_clock::now();
		if (now - m_last_caster_limit_warning > std::chrono::seconds(5)) {
			m_last_caster_limit_warning = now;
			TOAST_WARN(
			    "Render",
			    "ShadowPass: {} voxel casters, only the first {} cast shadows",
			    frame->voxel_instances.size(),
			    k_max_voxel_casters
			);
		}
	}
	++m_caster_frame;
	const float tolerance = shadows::casterTolerance();
	m_caster_models.resize(count);
	m_caster_moving.resize(count);
	for (uint32_t i = 0; i < count; ++i) {
		const auto& proxy = frame->voxel_instances[i];
		auto [it, inserted] = m_settled_casters.try_emplace(proxy.node_rid);
		SettledCaster& settled = it->second;
		const glm::vec3 extent = glm::vec3(proxy.brick_dims) * voxel::k_brick_size;
		if (inserted || cornerDrift(settled.model, proxy.model, extent) > tolerance) {
			settled.model = proxy.model;
			settled.inverse_model = proxy.inverse_model;
			settled.moved_frame = m_caster_frame;
		}
		settled.seen_frame = m_caster_frame;
		m_caster_models[i] = settled.model;
		m_caster_moving[i] = m_caster_frame - settled.moved_frame < k_caster_settle_frames ? 1u : 0u;

		// Shadows never sample motion so whether a real previous transform exists does not matter here
		instances[i] = makeVoxelInstance(settled.model, settled.inverse_model, settled.model, true, proxy.record_index);
	}
	allocation.flush(0, sizeof(VoxelInstanceGpu) * count);

	if (m_caster_frame % k_caster_settle_frames == 0) {
		std::erase_if(m_settled_casters, [&](const auto& entry) {
			return m_caster_frame - entry.second.seen_frame > k_caster_settle_frames;
		});
	}
	return count;
}

auto ShadowPass::voxelPipelineSetFor(uint32_t view_mask) const -> const VoxelPipelineSet* {
	if (!m_voxels_ready) {
		return nullptr;
	}
	for (const VoxelPipelineSet& set : m_voxel_pipeline_sets) {
		if (set.view_mask == view_mask) {
			return &set;
		}
	}
	return nullptr;
}

auto ShadowPass::getCascadeMapView(uint32_t frame_index) const -> vk::ImageView {
	if (frame_index >= m_targets.size() || !m_targets[frame_index].cascades.array_view.has_value()) {
		return nullptr;
	}
	return **m_targets[frame_index].cascades.array_view;
}

auto ShadowPass::getPunctualMapView(uint32_t frame_index) const -> vk::ImageView {
	if (frame_index >= m_targets.size() || !m_targets[frame_index].punctual.array_view.has_value()) {
		return nullptr;
	}
	return **m_targets[frame_index].punctual.array_view;
}

void ShadowPass::recordMap(vk::CommandBuffer cmd, ShadowMap& map, ShadowMap& static_map, uint32_t frame_index, bool directional) {
	ZoneScoped;
	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr || !map.image.has_value() || !static_map.image.has_value()) {
		return;
	}

	// MaterialPass still binds these and an eUndefined image is invalid even unread
	if (frame->frame_data.traced_shadow_params.x >= 0.5f && map.state.usage == sync::Usage::fragment_sampled) {
		return;
	}

	std::vector<const VulkanRenderer::ShadowView*> layer_views(map.layer_count, nullptr);
	std::vector<uint32_t> layer_view_indices(map.layer_count, 0);
	for (uint32_t i = 0; i < frame->shadows.views.size(); ++i) {
		const auto& candidate = frame->shadows.views[i];
		if (candidate.directional == directional && candidate.layer < map.layer_count) {
			layer_views[candidate.layer] = &candidate;
			layer_view_indices[candidate.layer] = i;
		}
	}

	const auto& proxies = frame->mesh_instances;
	const size_t instance_count = frame->shadow_instance_data.size();
	const vk::Buffer posed_vertices = VulkanRenderer::instance->getPosedVertexBuffer(frame_index);

	const auto posed_offset_of = [&](const VulkanRenderer::MeshInstanceProxy& proxy) {
		return posed_vertices ? proxy.posed_vertex_offset : VulkanRenderer::MeshInstanceProxy::k_no_posed_vertices;
	};

	const auto occupied = [&](const LayerGroup& group) {
		bool result = layer_views[group.base_layer] != nullptr;
		for (uint32_t i = 1; result && i < group.layer_count; ++i) {
			const auto* member = layer_views[group.base_layer + i];
			result = member != nullptr && layer_view_indices[group.base_layer + i] == layer_view_indices[group.base_layer] + i;
		}
		return result;
	};

	const float receiver_depth_radii = shadows::cascadeReceiverDepthRadii();
	const auto reaches = [&](const LayerGroup& group, const glm::vec3& center, float radius) {
		for (uint32_t i = 0; i < group.layer_count; ++i) {
			const auto* member = layer_views[group.base_layer + i];
			if (member == nullptr) {
				continue;
			}
			const glm::vec3 offset = center - glm::vec3(member->cull_sphere);
			const float reach = member->cull_sphere.w + radius;
			if (!member->directional) {
				if (glm::dot(offset, offset) <= (reach * reach)) {
					return true;
				}
				continue;
			}

			// Any distance toward the sun since clamped depth keeps casters in front
			const auto& m = member->view_projection;
			const glm::vec3 away = glm::normalize(glm::vec3(m[0][2], m[1][2], m[2][2]));
			const float along = glm::dot(offset, away);
			const glm::vec3 lateral = offset - (away * along);
			const float depth_reach = (member->cull_sphere.w * receiver_depth_radii) + radius;
			if (along <= depth_reach && glm::dot(lateral, lateral) <= (reach * reach)) {
				return true;
			}
		}
		return false;
	};

	const auto eligible = [&](const LayerGroup& group, size_t index) {
		const auto& proxy = proxies[index];
		return proxy.mesh != nullptr && proxy.mesh->isReady() && reaches(group, proxy.bounds_center, proxy.bounds_radius);
	};

	const auto& voxel_proxies = frame->voxel_instances;
	const auto voxel_eligible = [&](const LayerGroup& group, size_t index) {
		return reaches(group, voxel_proxies[index].bounds_center, voxel_proxies[index].bounds_radius);
	};

	// Without the cache every voxel caster draws with the moving ones into each slot
	const bool static_cache = shadows::staticCasterCache();
	const auto voxel_moving = [&](size_t index) { return !static_cache || m_caster_moving[index] != 0; };

	// Summed since voxel proxies are sorted by camera distance and a static light must not re-render as the camera moves
	const auto voxel_signature = [&](const LayerGroup& group, bool moving, std::vector<VoxelCaster>* casters) -> uint64_t {
		uint64_t sum = 0;
		for (size_t index = 0; index < m_voxel_instance_count; ++index) {
			if (voxel_moving(index) != moving || !voxel_eligible(group, index)) {
				continue;
			}
			ShadowSignature caster;
			caster.add(voxel_proxies[index].node_rid);
			caster.add(m_caster_models[index]);
			sum += caster.value;
			if (casters != nullptr) {
				casters->push_back({.node_rid = voxel_proxies[index].node_rid, .model = m_caster_models[index]});
			}
		}
		return sum;
	};

	const auto view_signature_of = [&](const LayerGroup& group) -> uint64_t {
		ShadowSignature signature;
		for (uint32_t i = 0; i < group.layer_count; ++i) {
			const auto* member = layer_views[group.base_layer + i];
			signature.add(member->view_projection);
			signature.add(member->resolution);
		}
		return signature.value;
	};

	/// Empty while a posed mesh is eligible since its pose changes without its transform
	const auto moving_signature_of = [&](const LayerGroup& group, const LayerGroup& static_group) -> std::optional<uint64_t> {
		ShadowSignature signature;
		signature.add(*static_group.signature);
		signature.add(*static_group.caster_signature);
		signature.add(static_group.revision);
		for (size_t index = 0; index < proxies.size() && index < instance_count; ++index) {
			if (!eligible(group, index)) {
				continue;
			}
			const auto& proxy = proxies[index];
			if (posed_offset_of(proxy) != VulkanRenderer::MeshInstanceProxy::k_no_posed_vertices) {
				return std::nullopt;
			}
			signature.add(static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(proxy.mesh)));
			signature.add(proxy.model);
		}

		uint64_t voxel_casters = 0;
		for (size_t index = 0; index < m_voxel_instance_count; ++index) {
			if (!voxel_eligible(group, index)) {
				continue;
			}
			ShadowSignature caster;
			caster.add(voxel_proxies[index].node_rid);
			caster.add(m_caster_models[index]);
			voxel_casters += caster.value;
			m_voxel_eligible.push_back({.node_rid = voxel_proxies[index].node_rid, .model = m_caster_models[index]});
		}
		if (!m_voxel_eligible.empty()) {
			signature.add(voxel_casters);
		}
		return signature.value;
	};

	const std::optional<uint64_t> voxel_generation =
	    frame->voxel_storage != nullptr ? std::optional {frame->voxel_storage->generation()} : std::nullopt;

	/// A region published since the group last matched reaches one of its views
	const auto voxel_changed = [&](const LayerGroup& group) -> bool {
		if (!group.voxel_generation.has_value() || frame->voxel_storage == nullptr) {
			return true;
		}

		std::array<FrustumPlanes, shadows::k_cube_faces> view_planes;
		size_t view_count = 0;
		for (uint32_t i = 0; i < group.layer_count && view_count < view_planes.size(); ++i) {
			if (const auto* member = layer_views[group.base_layer + i]; member != nullptr) {
				view_planes[view_count++] = extractFrustumPlanes(member->view_projection);
			}
		}
		return regionsReach(
		    frame->voxel_storage->changeHistory(),
		    *group.voxel_generation,
		    m_voxel_eligible,
		    std::span {view_planes.data(), view_count}
		);
	};

	enum class GroupWork : uint8_t {
		skip,
		clear,
		/// Static copy then the moving casters
		compose,
	};

	std::vector<GroupWork> group_work(map.groups.size(), GroupWork::skip);
	std::vector<std::optional<uint64_t>> group_signatures(map.groups.size());
	std::vector<bool> static_render(map.groups.size(), false);
	bool any_static_render = false;
	bool any_slot_work = false;
	for (size_t g = 0; g < map.groups.size(); ++g) {
		auto& group = map.groups[g];
		auto& static_group = static_map.groups[g];
		if (!occupied(group)) {
			group_work[g] = group.dirty ? GroupWork::clear : GroupWork::skip;
			any_slot_work = any_slot_work || group.dirty;
			continue;
		}

		m_voxel_eligible.clear();
		const uint64_t view_signature = view_signature_of(group);
		const uint64_t caster_signature = voxel_signature(group, false, &m_voxel_eligible);
		const bool has_voxels = !m_voxel_eligible.empty();
		if (static_group.signature != view_signature) {
			++m_stats.static_views;
		} else if (static_group.caster_signature != caster_signature) {
			++m_stats.static_casters;
		} else if (has_voxels && voxel_changed(static_group)) {
			++m_stats.static_edits;
		} else {
			if (has_voxels && static_group.voxel_generation != voxel_generation) {
				++m_stats.spared;
			}
			static_group.voxel_generation = voxel_generation;
			continue;
		}
		static_render[g] = true;
		any_static_render = true;
		static_group.signature = view_signature;
		static_group.caster_signature = caster_signature;
		static_group.voxel_generation = voxel_generation;
		++static_group.revision;
	}
	for (size_t g = 0; g < map.groups.size(); ++g) {
		auto& group = map.groups[g];
		if (!occupied(group)) {
			continue;
		}
		group_signatures[g] = moving_signature_of(group, static_map.groups[g]);
		if (group_signatures[g].has_value() && group.signature == group_signatures[g]) {
			++m_stats.cached;
		} else {
			++m_stats.composed;
			group_work[g] = GroupWork::compose;
			any_slot_work = true;
		}
	}
	if (!any_static_render && !any_slot_work) {
		return;
	}

	const auto bind_viewport = [&](const VulkanRenderer::ShadowView& view, uint32_t resolution) {
		const uint32_t view_resolution = std::clamp(view.resolution, 1u, resolution);
		const vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(view_resolution), static_cast<float>(view_resolution), 0.0f, 1.0f);
		cmd.setViewport(0, std::array {viewport});
		cmd.setScissor(0, std::array {vk::Rect2D({0, 0}, vk::Extent2D {view_resolution, view_resolution})});
	};

	const auto begin_group = [&](const ShadowMap& target, const LayerGroup& group, vk::AttachmentLoadOp load) {
		vk::RenderingAttachmentInfo depth_attachment {};
		depth_attachment.imageView = **group.view;
		depth_attachment.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
		depth_attachment.loadOp = load;
		depth_attachment.storeOp = vk::AttachmentStoreOp::eStore;
		depth_attachment.clearValue = vk::ClearValue(vk::ClearDepthStencilValue {1.0f, 0});

		vk::RenderingInfo rendering_info {};
		rendering_info.renderArea = vk::Rect2D({0, 0}, vk::Extent2D {target.resolution, target.resolution});
		// Ignored when viewMask is non zero
		rendering_info.layerCount = 1;
		rendering_info.viewMask = group.view_mask;
		rendering_info.pDepthAttachment = &depth_attachment;
		cmd.beginRendering(rendering_info);
		++m_stats.scopes;
	};

	const auto draw_meshes = [&](const LayerGroup& group, uint32_t view_index) {
		const PipelineSet* pipelines = pipelineSetFor(group.view_mask);
		if (pipelines == nullptr || !pipelines->pipeline.isReady()) {
			return;
		}
		cmd.bindDescriptorSets(
		    vk::PipelineBindPoint::eGraphics,
		    *m_shader_layout.getPipelineLayout(),
		    0,
		    std::array<vk::DescriptorSet, 1> {*m_descriptor_sets[frame_index]},
		    {}
		);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines->pipeline.getPipeline());

		constexpr uint32_t k_push_size = 2 * sizeof(uint32_t);
		for (size_t i = 0; i < proxies.size() && i < instance_count;) {
			if (!eligible(group, i)) {
				++i;
				continue;
			}

			const uint32_t posed_offset = posed_offset_of(proxies[i]);
			const bool posed = posed_offset != VulkanRenderer::MeshInstanceProxy::k_no_posed_vertices;

			size_t run = 1;
			if (!posed) {
				while (i + run < proxies.size() && i + run < instance_count && eligible(group, i + run) &&
				       proxies[i + run].mesh == proxies[i].mesh &&
				       posed_offset_of(proxies[i + run]) == VulkanRenderer::MeshInstanceProxy::k_no_posed_vertices) {
					++run;
				}
			}

			const ShadowPushConstants push {
			  .instance_base = static_cast<uint32_t>(i),
			  .view_index = view_index,
			};
			cmd.pushConstants(*m_shader_layout.getPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, k_push_size, &push);

			if (posed) {
				proxies[i].mesh->bindPosed(cmd, posed_vertices, posed_offset);
			} else {
				proxies[i].mesh->bind(cmd);
			}
			proxies[i].mesh->draw(cmd, static_cast<uint32_t>(run));
			++m_stats.draws;

			i += run;
		}
	};

	// Only a volume past the device task dispatch limits marches when mesh shaders are available
	const auto meshes_volume = [&](const VulkanRenderer::VoxelVolumeProxy& proxy) {
		const glm::uvec3 coarse = voxel::gpu::coarseDims(proxy.brick_dims);
		const uint64_t groups_x = uint64_t {coarse.x} * k_task_groups_per_cell;
		return groups_x <= m_max_task_groups[0] && coarse.y <= m_max_task_groups[1] && coarse.z <= m_max_task_groups[2] &&
		       groups_x * coarse.y * coarse.z <= m_max_task_group_total;
	};

	const auto draw_voxels =
	    [&](const LayerGroup& group, const VulkanRenderer::ShadowView& view, uint32_t view_index, bool moving) {
		    const VoxelPipelineSet* voxel_pipelines = voxelPipelineSetFor(group.view_mask);
		    if (voxel_pipelines == nullptr || m_voxel_instance_count == 0) {
			    return;
		    }
		    const vk::PipelineLayout voxel_layout = *m_voxel_layout.getPipelineLayout();
		    const vk::PipelineLayout mesh_layout = *m_voxel_mesh_layout.getPipelineLayout();
		    const VulkanPipeline* mesh_pipeline =
		        m_draw_mesh_tasks != nullptr && shadows::voxelMeshShadows() ? voxelMeshPipelineFor(group.view_mask) : nullptr;

		    const auto sets =
		        std::array<vk::DescriptorSet, 2> {*m_voxel_shadow_sets[frame_index], *m_voxel_storage_sets[frame_index]};

		    // Meshed first since their quads keep early depth for the marched volumes behind them
		    m_voxel_draw_order.clear();
		    bool meshes_bound = false;
		    for (uint32_t index = 0; index < m_voxel_instance_count; ++index) {
			    if (voxel_moving(index) != moving || !voxel_eligible(group, index)) {
				    continue;
			    }
			    const auto& proxy = voxel_proxies[index];
			    if (mesh_pipeline == nullptr || !meshes_volume(proxy)) {
				    m_voxel_draw_order.push_back(index);
				    continue;
			    }

			    if (!meshes_bound) {
				    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, mesh_pipeline->getPipeline());
				    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mesh_layout, 0, sets, {});
				    meshes_bound = true;
			    }
			    const VoxelMeshShadowPush push {
			      .instance_index = index,
			      .view_index = view_index,
			      .view_count = group.layer_count,
			      .directional = directional ? 1u : 0u,
			    };
			    cmd.pushConstants(mesh_layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(VoxelMeshShadowPush), &push);
			    const glm::uvec3 coarse = voxel::gpu::coarseDims(proxy.brick_dims);
			    m_draw_mesh_tasks(static_cast<VkCommandBuffer>(cmd), coarse.x * k_task_groups_per_cell, coarse.y, coarse.z);
			    ++m_stats.draws;
			    ++m_stats.meshed_volumes;
		    }
		    if (m_voxel_draw_order.empty()) {
			    return;
		    }

		    // Nearest to the light first by clip z which grows with distance under both projections
		    std::ranges::sort(m_voxel_draw_order, {}, [&](uint32_t index) {
			    return (view.view_projection * glm::vec4(voxel_proxies[index].bounds_center, 1.0f)).z;
		    });

		    // A perspective near plane clips front faces so only exact back faces are safe and cascades clamp
		    const auto crosses_near_plane = [&](uint32_t index) {
			    if (directional && m_core->isDepthClampSupported()) {
				    return false;
			    }
			    const glm::vec3 extent = glm::vec3(voxel_proxies[index].brick_dims) * voxel::k_brick_size;
			    for (uint32_t i = 0; i < group.layer_count; ++i) {
				    const auto* member = layer_views[group.base_layer + i];
				    if (member == nullptr) {
					    continue;
				    }
				    const glm::mat4 to_clip = member->view_projection * m_caster_models[index];
				    for (uint32_t corner = 0; corner < 8; ++corner) {
					    const glm::vec3 local(
					        (corner & 1u) != 0 ? extent.x : 0.0f, (corner & 2u) != 0 ? extent.y : 0.0f, (corner & 4u) != 0 ? extent.z : 0.0f
					    );
					    if ((to_clip * glm::vec4(local, 1.0f)).z < 0.0f) {
						    return true;
					    }
				    }
			    }
			    return false;
		    };

		    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, voxel_layout, 0, sets, {});
		    const VulkanPipeline* bound = nullptr;
		    for (const uint32_t index : m_voxel_draw_order) {
			    const auto& proxy = voxel_proxies[index];
			    const bool inside = crosses_near_plane(index);
			    const bool cull_front = inside != proxy.mirrored;
			    const VulkanPipeline& pipeline = voxel_pipelines->pipelines[cull_front ? 1 : 0][inside ? 1 : 0];
			    if (bound != &pipeline) {
				    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getPipeline());
				    bound = &pipeline;
			    }

			    const VoxelShadowPush push {
			      .instance_index = index,
			      .view_index = view_index,
			      .constant_bias = m_voxel_constant_bias,
			      .directional = directional ? 1u : 0u,
			    };
			    cmd.pushConstants(voxel_layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(VoxelShadowPush), &push);
			    cmd.draw(36, 1, 0, 0);
			    ++m_stats.draws;
			    ++m_stats.marched_volumes;
		    }
	    };

	if (any_static_render) {
		sync::transition(cmd, static_map.state, sync::Usage::depth_attachment);
		for (size_t g = 0; g < static_map.groups.size(); ++g) {
			if (!static_render[g]) {
				continue;
			}
			const auto& group = static_map.groups[g];
			const auto& view = *layer_views[group.base_layer];
			begin_group(static_map, group, vk::AttachmentLoadOp::eClear);
			bind_viewport(view, static_map.resolution);
			draw_voxels(group, view, layer_view_indices[group.base_layer], false);
			cmd.endRendering();
		}
	}

	if (!any_slot_work) {
		return;
	}

	m_static_copies.clear();
	for (size_t g = 0; g < map.groups.size(); ++g) {
		if (group_work[g] != GroupWork::compose) {
			continue;
		}
		const auto& group = map.groups[g];
		const vk::ImageSubresourceLayers layers(vk::ImageAspectFlagBits::eDepth, 0, group.base_layer, group.layer_count);
		m_static_copies.emplace_back(
		    layers, vk::Offset3D {}, layers, vk::Offset3D {}, vk::Extent3D {map.resolution, map.resolution, 1}
		);
	}
	if (!m_static_copies.empty()) {
		sync::BarrierBatch barriers;
		barriers.image(static_map.state, sync::Usage::transfer_src);
		barriers.image(map.state, sync::Usage::transfer_dst);
		barriers.flush(cmd);
		cmd.copyImage(
		    **static_map.image,
		    vk::ImageLayout::eTransferSrcOptimal,
		    **map.image,
		    vk::ImageLayout::eTransferDstOptimal,
		    m_static_copies
		);
	}

	// From the tracked usage so skipped groups keep their depth
	sync::transition(cmd, map.state, sync::Usage::depth_attachment);

	for (size_t g = 0; g < map.groups.size(); ++g) {
		if (group_work[g] == GroupWork::skip) {
			continue;
		}

		auto& group = map.groups[g];
		const bool compose = group_work[g] == GroupWork::compose;
		group.dirty = compose;
		group.signature = compose ? group_signatures[g] : std::nullopt;

		begin_group(map, group, compose ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear);
		if (compose) {
			const auto& view = *layer_views[group.base_layer];
			const uint32_t view_index = layer_view_indices[group.base_layer];
			bind_viewport(view, map.resolution);
			draw_meshes(group, view_index);
			draw_voxels(group, view, view_index, true);
		}
		cmd.endRendering();
	}

	// Bound in eShaderReadOnlyOptimal by the lighting descriptors
	sync::transition(cmd, map.state, sync::Usage::fragment_sampled);
}

void ShadowPass::recordPre(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) {
	ZoneScoped;
	(void)image_index;

	const bool pipelines_ready = !m_pipeline_sets.empty() && std::ranges::all_of(m_pipeline_sets, [](const PipelineSet& set) {
		return set.pipeline.isReady();
	});
	if (!isEnabled() || !pipelines_ready || frame_index >= m_targets.size()) {
		return;
	}

	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr) {
		return;
	}

	auto& target = m_targets[frame_index];

	m_stats = {};

	if (target.ubo.gpu_buffer.has_value()) {
		ShadowUBO ubo {};
		const size_t view_count = std::min<size_t>(frame->shadows.matrices.size(), shadows::k_max_shadow_views);
		std::copy_n(frame->shadows.matrices.begin(), view_count, ubo.view_projection.begin());
		for (size_t i = 0; i < view_count; ++i) {
			ubo.inverse_view_projection[i] = glm::inverse(ubo.view_projection[i]);
		}

		const auto& allocation = target.ubo.gpu_buffer->getAllocation();
		if (auto* mapped = allocation.getInfo().pMappedData) {
			std::memcpy(mapped, &ubo, sizeof(ShadowUBO));
			allocation.flush(0, sizeof(ShadowUBO));
		}
	}

	m_voxel_instance_count = prepareVoxels(frame_index);
	for (uint32_t index = 0; index < m_voxel_instance_count; ++index) {
		m_stats.moving_volumes += m_caster_moving[index];
	}

	recordMap(cmd, target.cascades, m_static.cascades, frame_index, true);
	recordMap(cmd, target.punctual, m_static.punctual, frame_index, false);
}

}
