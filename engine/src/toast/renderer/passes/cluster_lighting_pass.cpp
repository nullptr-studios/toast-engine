/// @file cluster_lighting_pass.cpp
/// @author dario
/// @date 18/07/2026

#include "cluster_lighting_pass.hpp"

#include "../shader_cache.hpp"
#include "../vulkan_core.hpp"
#include "../vulkan_debug.hpp"
#include "../vulkan_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <span>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

auto ClusterLightingPass::createBuffer(
    const renderer::VulkanCore& core, vk::DeviceSize size, vk::BufferUsageFlags usage, HostAccess access
) -> vma::raii::Buffer {
	const uint32_t graphics_family = core.getGraphicsQueueFamilyIndex();
	const uint32_t compute_family = core.getComputeQueueFamilyIndex();
	const bool use_concurrent_sharing = graphics_family != compute_family;

	vk::BufferCreateInfo buffer_ci {};
	buffer_ci.size = size;
	buffer_ci.usage = usage;

	std::array<uint32_t, 2> family_indices {graphics_family, compute_family};
	if (use_concurrent_sharing) {
		buffer_ci.sharingMode = vk::SharingMode::eConcurrent;
		buffer_ci.queueFamilyIndexCount = 2;
		buffer_ci.pQueueFamilyIndices = family_indices.data();
	} else {
		buffer_ci.sharingMode = vk::SharingMode::eExclusive;
	}

	vma::AllocationCreateInfo alloc_ci {};
	switch (access) {
		case HostAccess::write:
			alloc_ci.usage = vma::MemoryUsage::eAutoPreferHost;
			alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;
			break;
		case HostAccess::read:
			alloc_ci.usage = vma::MemoryUsage::eAutoPreferHost;
			alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessRandom;
			break;
		case HostAccess::none: alloc_ci.usage = vma::MemoryUsage::eAutoPreferDevice; break;
	}

	return core.getAllocator().createBuffer(buffer_ci, alloc_ci);
}

ClusterLightingPass::ClusterLightingPass(const renderer::VulkanCore& core) {
	ZoneScoped;
	const auto uid = assets::resolveURI("core://shaders/cluster_lighting.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "ClusterLightingPass shader core://shaders/cluster_lighting.slang unavailable, lighting will not run");
		return;
	}

	m_shader_layout.rebuild(core, shader->reflection, "ClusterLightingPass");

	VulkanPipeline::Config build_config;
	build_config.pipeline_type = VulkanPipeline::PipelineType::compute;
	build_config.debug_name = "ClusterLightingPass BuildClusters";
	build_config.shader_spirv = shader->spirv;
	build_config.pipeline_layout = *m_shader_layout.getPipelineLayout();
	build_config.compute_entry = "clusterBuildMain";
	m_build_clusters_pipeline.rebuild(core, build_config);

	VulkanPipeline::Config cull_config = build_config;
	cull_config.debug_name = "ClusterLightingPass CullLights";
	cull_config.compute_entry = "lightCullMain";
	m_cull_lights_pipeline.rebuild(core, cull_config);

	createResources(core);
}

void ClusterLightingPass::createResources(const renderer::VulkanCore& core) {
	ZoneScoped;
	using namespace clustered_lighting;

	const auto& device = core.getDevice();
	const auto& layouts = m_shader_layout.getDescriptorSetLayouts();
	if (layouts.empty()) {
		TOAST_CRITICAL("ClusterLightingPass", "ShaderLayout has no descriptor set layouts");
		return;
	}

	const vk::DescriptorSetLayout set_layout = *layouts[0];
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();

	m_frame_buffers.resize(VulkanRenderer::k_frames_in_flight);
	m_descriptor_sets.clear();
	m_descriptor_sets.reserve(VulkanRenderer::k_frames_in_flight);

	constexpr vk::DeviceSize cluster_params_size = sizeof(ClusterParamsGpu);
	const vk::DeviceSize lights_size = sizeof(VulkanRenderer::GpuLight) * k_max_lights;
	const vk::DeviceSize cluster_aabb_size = sizeof(ClusterAabbGpu) * k_cluster_count;
	const vk::DeviceSize cluster_light_grid_size = sizeof(uint32_t) * k_cluster_count;
	const vk::DeviceSize light_index_list_size =
	    sizeof(uint32_t) * static_cast<vk::DeviceSize>(k_cluster_count) * k_max_lights_per_cluster;

	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		auto& fb = m_frame_buffers[i];

		fb.cluster_params.gpu_buffer.emplace(
		    createBuffer(core, cluster_params_size, vk::BufferUsageFlagBits::eUniformBuffer, HostAccess::write)
		);
		setDebugName(core, **fb.cluster_params.gpu_buffer, std::format("ClusterLightingPass ClusterParams[{}]", i));

		fb.lights.gpu_buffer.emplace(createBuffer(core, lights_size, vk::BufferUsageFlagBits::eStorageBuffer, HostAccess::write));
		setDebugName(core, **fb.lights.gpu_buffer, std::format("ClusterLightingPass Lights[{}]", i));

		fb.cluster_aabb.gpu_buffer.emplace(
		    createBuffer(core, cluster_aabb_size, vk::BufferUsageFlagBits::eStorageBuffer, HostAccess::none)
		);
		setDebugName(core, **fb.cluster_aabb.gpu_buffer, std::format("ClusterLightingPass ClusterAABB[{}]", i));

		fb.cluster_light_grid.gpu_buffer.emplace(createBuffer(
		    core,
		    cluster_light_grid_size,
		    vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
		    HostAccess::none
		));
		setDebugName(core, **fb.cluster_light_grid.gpu_buffer, std::format("ClusterLightingPass ClusterLightGrid[{}]", i));

		fb.cluster_light_grid_readback.gpu_buffer.emplace(
		    createBuffer(core, cluster_light_grid_size, vk::BufferUsageFlagBits::eTransferDst, HostAccess::read)
		);
		setDebugName(
		    core, **fb.cluster_light_grid_readback.gpu_buffer, std::format("ClusterLightingPass ClusterLightGridReadback[{}]", i)
		);

		fb.light_index_list.gpu_buffer.emplace(
		    createBuffer(core, light_index_list_size, vk::BufferUsageFlagBits::eStorageBuffer, HostAccess::none)
		);
		setDebugName(core, **fb.light_index_list.gpu_buffer, std::format("ClusterLightingPass LightIndexList[{}]", i));

		const vk::DescriptorSetAllocateInfo alloc_info(pool, 1, &set_layout);
		auto allocated = device.allocateDescriptorSets(alloc_info);
		m_descriptor_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_descriptor_sets[i], std::format("ClusterLightingPass DescriptorSet[{}]", i));

		const vk::DescriptorBufferInfo params_info(**fb.cluster_params.gpu_buffer, 0, cluster_params_size);
		const vk::DescriptorBufferInfo lights_info(**fb.lights.gpu_buffer, 0, lights_size);
		const vk::DescriptorBufferInfo cluster_aabb_info(**fb.cluster_aabb.gpu_buffer, 0, cluster_aabb_size);
		const vk::DescriptorBufferInfo cluster_light_grid_info(**fb.cluster_light_grid.gpu_buffer, 0, cluster_light_grid_size);
		const vk::DescriptorBufferInfo light_index_list_info(**fb.light_index_list.gpu_buffer, 0, light_index_list_size);

		const std::array<vk::WriteDescriptorSet, 5> writes {
		  vk::WriteDescriptorSet(*m_descriptor_sets[i], 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &params_info),
		  vk::WriteDescriptorSet(*m_descriptor_sets[i], 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &lights_info),
		  vk::WriteDescriptorSet(*m_descriptor_sets[i], 2, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &cluster_aabb_info),
		  vk::WriteDescriptorSet(
		      *m_descriptor_sets[i], 3, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &cluster_light_grid_info
		  ),
		  vk::WriteDescriptorSet(*m_descriptor_sets[i], 4, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &light_index_list_info),
		};
		device.updateDescriptorSets(writes, {});
	}
}

void ClusterLightingPass::update(uint32_t frame_index, float dt) {
	ZoneScoped;
	(void)dt;

	if (frame_index >= m_frame_buffers.size()) {
		return;
	}

	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr) {
		return;
	}

	using namespace clustered_lighting;
	auto& fb = m_frame_buffers[frame_index];

	const uint32_t light_count = std::min<uint32_t>(static_cast<uint32_t>(frame->lights.size()), k_max_lights);

	const float log_ratio = std::max(std::log(frame->cluster_far / frame->cluster_near), 0.001f);

	ClusterParamsGpu params {};
	params.inverse_projection = glm::inverse(frame->frame_data.projection);
	params.cluster_dims = glm::uvec4(k_cluster_dim_x, k_cluster_dim_y, k_cluster_dim_z, k_max_lights_per_cluster);
	params.screen_size_near_far = glm::vec4(frame->viewport_extent, frame->camera_near, frame->camera_far);
	params.slice_depth = glm::vec4(frame->cluster_near, frame->cluster_far, static_cast<float>(k_cluster_dim_z) / log_ratio, 0.0f);
	params.light_counts = glm::uvec4(light_count, 0, 0, 0);

	if (fb.cluster_params.gpu_buffer.has_value()) {
		void* mapped = fb.cluster_params.gpu_buffer->getAllocation().getInfo().pMappedData;
		std::memcpy(mapped, &params, sizeof(ClusterParamsGpu));
		fb.cluster_params.gpu_buffer->getAllocation().flush(0, sizeof(ClusterParamsGpu));
	}

	if (light_count > 0 && fb.lights.gpu_buffer.has_value()) {
		void* mapped = fb.lights.gpu_buffer->getAllocation().getInfo().pMappedData;
		const auto bytes = static_cast<vk::DeviceSize>(light_count) * sizeof(VulkanRenderer::GpuLight);
		std::memcpy(mapped, frame->lights.data(), bytes);
		fb.lights.gpu_buffer->getAllocation().flush(0, bytes);
	}
}

void ClusterLightingPass::dispatch(vk::CommandBuffer cmd, uint32_t frame_index) {
	ZoneScoped;
	if (frame_index >= m_descriptor_sets.size() || !m_build_clusters_pipeline.isReady() || !m_cull_lights_pipeline.isReady()) {
		return;
	}

	using namespace clustered_lighting;

	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eCompute,
	    *m_shader_layout.getPipelineLayout(),
	    0,
	    std::array<vk::DescriptorSet, 1> {*m_descriptor_sets[frame_index]},
	    {}
	);

	constexpr uint32_t k_workgroup_size = 64;
	constexpr uint32_t group_count = (k_cluster_count + k_workgroup_size - 1) / k_workgroup_size;

	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_build_clusters_pipeline.getPipeline());
	cmd.dispatch(group_count, 1, 1);

	// Same command buffer does not order shader memory access so this needs its own barrier
	auto& fb = m_frame_buffers[frame_index];
	const vk::BufferMemoryBarrier barrier(
	    vk::AccessFlagBits::eShaderWrite,
	    vk::AccessFlagBits::eShaderRead,
	    VK_QUEUE_FAMILY_IGNORED,
	    VK_QUEUE_FAMILY_IGNORED,
	    **fb.cluster_aabb.gpu_buffer,
	    0,
	    VK_WHOLE_SIZE
	);
	cmd.pipelineBarrier(
	    vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, {}, nullptr, barrier, nullptr
	);

	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_cull_lights_pipeline.getPipeline());
	cmd.dispatch(group_count, 1, 1);

	fb.readback_valid =
	    m_readback_hold.load(std::memory_order_relaxed) > 0 && fb.cluster_light_grid_readback.gpu_buffer.has_value();
	if (fb.readback_valid) {
		m_readback_hold.fetch_sub(1, std::memory_order_relaxed);

		const vk::BufferMemoryBarrier to_transfer(
		    vk::AccessFlagBits::eShaderWrite,
		    vk::AccessFlagBits::eTransferRead,
		    VK_QUEUE_FAMILY_IGNORED,
		    VK_QUEUE_FAMILY_IGNORED,
		    **fb.cluster_light_grid.gpu_buffer,
		    0,
		    VK_WHOLE_SIZE
		);
		cmd.pipelineBarrier(
		    vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer, {}, nullptr, to_transfer, nullptr
		);

		constexpr vk::DeviceSize k_grid_size = sizeof(uint32_t) * k_cluster_count;
		const vk::BufferCopy region(0, 0, k_grid_size);
		cmd.copyBuffer(**fb.cluster_light_grid.gpu_buffer, **fb.cluster_light_grid_readback.gpu_buffer, region);
	}
}

auto ClusterLightingPass::getClusterParamsBuffer(uint32_t frame_index) const -> vk::Buffer {
	if (frame_index >= m_frame_buffers.size() || !m_frame_buffers[frame_index].cluster_params.gpu_buffer.has_value()) {
		return nullptr;
	}
	return **m_frame_buffers[frame_index].cluster_params.gpu_buffer;
}

auto ClusterLightingPass::getLightsBuffer(uint32_t frame_index) const -> vk::Buffer {
	if (frame_index >= m_frame_buffers.size() || !m_frame_buffers[frame_index].lights.gpu_buffer.has_value()) {
		return nullptr;
	}
	return **m_frame_buffers[frame_index].lights.gpu_buffer;
}

auto ClusterLightingPass::getClusterLightGridBuffer(uint32_t frame_index) const -> vk::Buffer {
	if (frame_index >= m_frame_buffers.size() || !m_frame_buffers[frame_index].cluster_light_grid.gpu_buffer.has_value()) {
		return nullptr;
	}
	return **m_frame_buffers[frame_index].cluster_light_grid.gpu_buffer;
}

auto ClusterLightingPass::getLightIndexListBuffer(uint32_t frame_index) const -> vk::Buffer {
	if (frame_index >= m_frame_buffers.size() || !m_frame_buffers[frame_index].light_index_list.gpu_buffer.has_value()) {
		return nullptr;
	}
	return **m_frame_buffers[frame_index].light_index_list.gpu_buffer;
}

void ClusterLightingPass::requestGridReadback() const noexcept {
	constexpr uint32_t k_hold_frames = 4;
	m_readback_hold.store(k_hold_frames, std::memory_order_relaxed);
}

auto ClusterLightingPass::getClusterLightGridCounts(uint32_t frame_index) const -> std::span<const uint32_t> {
	if (frame_index >= m_frame_buffers.size() || !m_frame_buffers[frame_index].readback_valid) {
		return {};
	}
	const auto& readback = m_frame_buffers[frame_index].cluster_light_grid_readback.gpu_buffer;
	if (!readback.has_value()) {
		return {};
	}
	const auto* mapped = static_cast<const uint32_t*>(readback->getAllocation().getInfo().pMappedData);
	if (mapped == nullptr) {
		return {};
	}
	readback->getAllocation().invalidate(0, sizeof(uint32_t) * clustered_lighting::k_cluster_count);
	return {mapped, clustered_lighting::k_cluster_count};
}

auto ClusterLightingPass::summarizeGrid(std::span<const uint32_t> counts) -> GridStats {
	using namespace clustered_lighting;

	GridStats stats;
	std::array<uint32_t, k_cluster_count> occupied {};
	uint64_t total = 0;

	for (const uint32_t count : counts.first(std::min<size_t>(counts.size(), k_cluster_count))) {
		if (count == 0) {
			continue;
		}
		occupied[stats.occupied++] = count;
		total += count;
		stats.max_count = std::max(stats.max_count, count);
		if (count > k_max_lights_per_cluster) {
			++stats.overflowed;
			stats.dropped += count - k_max_lights_per_cluster;
		}
	}

	if (stats.occupied > 0) {
		stats.mean = static_cast<float>(total) / static_cast<float>(stats.occupied);

		// Fuckass MSVC
		const std::span<uint32_t> filled(occupied.data(), stats.occupied);
		const size_t p95 = (static_cast<size_t>(stats.occupied) * 95) / 100;
		std::ranges::nth_element(filled, filled.begin() + static_cast<std::ptrdiff_t>(p95));
		stats.p95 = filled[p95];
	}
	return stats;
}

}    // namespace renderer
