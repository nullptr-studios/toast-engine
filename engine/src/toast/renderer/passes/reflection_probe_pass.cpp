/**
 * @file reflection_probe_pass.cpp
 * @author dario
 * @date 05/08/2026
 */

#include "reflection_probe_pass.hpp"

#include "../cube_face_basis.hpp"
#include "../shader_cache.hpp"
#include "../vulkan_core.hpp"
#include "../vulkan_debug.hpp"
#include "../vulkan_renderer.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <limits>
#include <toast/assets/asset_manager.hpp>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

namespace {

/// eTransferSrc is needed to save a bake
constexpr vk::ImageUsageFlags k_probe_transfer_usage =
    vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;

constexpr uint32_t k_probe_mips = 5;

struct ProbeFileHeader {
	std::array<uint8_t, 6> magic = {'T', 'P', 'R', 'O', 'B', 'E'};
	uint16_t version = 1;
	uint32_t face_size = 0;
	uint32_t mip_levels = 0;
	uint32_t irradiance_size = 0;
	uint32_t format = 0;
};

}

ReflectionProbePass::ReflectionProbePass(const VulkanCore& core, vk::Format color_format, vk::Format depth_format)
    : m_core(&core),
      m_format(color_format),
      m_depth_format(depth_format) {
	ZoneScoped;
	const auto uid = assets::resolveURI("core://shaders/environment.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "ReflectionProbePass shader core://shaders/environment.slang unavailable; probes stay unbaked");
		setEnabled(false);
		return;
	}

	m_mip_levels = k_probe_mips;
	m_shader_layout.rebuild(core, shader->reflection, "ReflectionProbePass");

	const auto sampler_ci = linearClampMippedSamplerInfo(VK_LOD_CLAMP_NONE);
	m_sampler = vk::raii::Sampler(core.getDevice(), sampler_ci);
	setDebugName(core, *m_sampler, "ReflectionProbePass Sampler");

	m_staging.cube.create(core, m_format, k_max_face_size, 1, "ReflectionProbe Staging", k_probe_transfer_usage, false);

	m_probes.resize(k_max_probes);
	m_probe_irradiance.resize(k_max_probes);
	for (uint32_t i = 0; i < k_max_probes; ++i) {
		m_probes[i].cube.create(
		    core, m_format, k_default_face_size, m_mip_levels, std::format("ReflectionProbe[{}]", i), k_probe_transfer_usage
		);
		m_probe_irradiance[i].cube.create(
		    core, m_format, k_irradiance_size, 1, std::format("ReflectionProbe[{}] Irradiance", i), k_probe_transfer_usage
		);
	}

	createPipelines(core);
	createDescriptors(core);
	createShResources(core);

	TOAST_INFO(
	    "Render",
	    "ReflectionProbePass ready: {} probes at {}px (max {}), {} roughness levels",
	    k_max_probes,
	    k_default_face_size,
	    k_max_face_size,
	    m_mip_levels
	);
}

void ReflectionProbePass::createPipelines(const VulkanCore& core) {
	ZoneScoped;
	const auto uid = assets::resolveURI("core://shaders/environment.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		return;
	}

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.debug_name = "ReflectionProbePass Prefilter";
	config.color_format = m_format;
	config.extent = vk::Extent2D {1, 1};
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_shader_layout.getPipelineLayout();
	config.vertex_bindings = {};
	config.vertex_attributes = {};
	config.cull_mode = vk::CullModeFlagBits::eNone;
	config.depth_test = false;
	config.depth_write = false;
	config.fragment_entry = "fragmentPrefilter";
	m_prefilter_pipeline.rebuild(core, config);

	config.debug_name = "ReflectionProbePass Irradiance";
	config.fragment_entry = "fragmentIrradiance";
	m_irradiance_pipeline.rebuild(core, config);

	const auto preview_uid = assets::resolveURI("core://shaders/probe_preview.slang");
	const auto preview_shader = preview_uid.has_value() ? ShaderCache::get().acquire(*preview_uid) : nullptr;
	if (!preview_shader) {
		TOAST_WARN("Render", "core://shaders/probe_preview.slang unavailable; the Probe Cubemap view will draw nothing");
		return;
	}

	m_preview_layout.rebuild(core, preview_shader->reflection, "ReflectionProbePreview");

	VulkanPipeline::Config preview {};
	preview.pipeline_type = VulkanPipeline::PipelineType::graphics;
	preview.debug_name = "ReflectionProbePass Preview";
	preview.color_format = m_format;
	preview.depth_format = m_depth_format;
	preview.extra_color_formats = worldStageExtraColorFormats();
	preview.extent = vk::Extent2D {1, 1};
	preview.shader_spirv = preview_shader->spirv;
	preview.pipeline_layout = *m_preview_layout.getPipelineLayout();
	preview.vertex_bindings = {};
	preview.vertex_attributes = {};
	preview.cull_mode = vk::CullModeFlagBits::eBack;
	preview.depth_test = true;
	preview.depth_write = true;
	m_preview_pipeline.rebuild(core, preview);
}

void ReflectionProbePass::createDescriptors(const VulkanCore& core) {
	ZoneScoped;
	const auto& layouts = m_shader_layout.getDescriptorSetLayouts();
	if (layouts.empty() || !m_staging.cube.isReady()) {
		return;
	}

	const vk::DescriptorSetLayout set_layout = *layouts[0];
	const vk::DescriptorSetAllocateInfo alloc_info(VulkanRenderer::instance->getDescriptorPoolHandle(), 1, &set_layout);
	auto allocated = core.getDevice().allocateDescriptorSets(alloc_info);
	m_staging_source_set = std::move(allocated[0]);
	setDebugName(core, *m_staging_source_set, "ReflectionProbePass StagingSourceSet");

	vk::DescriptorImageInfo image_info {};
	image_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	image_info.imageView = m_staging.cube.cubeView();
	image_info.sampler = *m_sampler;

	const vk::WriteDescriptorSet write(*m_staging_source_set, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &image_info);
	core.getDevice().updateDescriptorSets(write, {});

	const auto& preview_layouts = m_preview_layout.getDescriptorSetLayouts();
	if (preview_layouts.empty()) {
		return;
	}

	std::vector<vk::DescriptorImageInfo> preview_infos;
	std::vector<vk::WriteDescriptorSet> preview_writes;
	preview_infos.reserve(m_probes.size());
	preview_writes.reserve(m_probes.size());

	for (auto& probe : m_probes) {
		const vk::DescriptorSetLayout preview_set_layout = *preview_layouts[0];
		const vk::DescriptorSetAllocateInfo preview_alloc(
		    VulkanRenderer::instance->getDescriptorPoolHandle(), 1, &preview_set_layout
		);
		auto preview_allocated = core.getDevice().allocateDescriptorSets(preview_alloc);
		m_preview_sets.push_back(std::move(preview_allocated[0]));

		vk::DescriptorImageInfo preview_info {};
		preview_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		preview_info.imageView = probe.cube.cubeView();
		preview_info.sampler = *m_sampler;
		preview_infos.push_back(preview_info);

		preview_writes.emplace_back(
		    *m_preview_sets.back(), 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &preview_infos.back()
		);
	}

	core.getDevice().updateDescriptorSets(preview_writes, {});
}

void ReflectionProbePass::captureFace(
    vk::CommandBuffer cmd, vk::Image scene_color, vk::Extent2D scene_extent, uint32_t probe, uint32_t face
) {
	if (probe >= m_probes.size() || face >= 6 || !m_staging.cube.isReady() || !isEnabled()) {
		TOAST_WARN(
		    "Render",
		    "Probe capture skipped (probe {}, face {}): staging={} enabled={}",
		    probe,
		    face,
		    m_staging.cube.isReady(),
		    isEnabled()
		);
		return;
	}

	m_staging.cube.transition(
	    cmd, vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eTransfer
	);

	vk::ImageBlit blit {};
	blit.srcSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
	blit.srcOffsets[0] = vk::Offset3D {0, 0, 0};
	blit.srcOffsets[1] = vk::Offset3D {static_cast<int32_t>(scene_extent.width), static_cast<int32_t>(scene_extent.height), 1};
	blit.dstSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, face, 1);
	blit.dstOffsets[0] = vk::Offset3D {0, 0, 0};
	blit.dstOffsets[1] = vk::Offset3D {static_cast<int32_t>(m_staging.cube.size()), static_cast<int32_t>(m_staging.cube.size()), 1};

	cmd.blitImage(
	    scene_color,
	    vk::ImageLayout::eTransferSrcOptimal,
	    m_staging.cube.image(),
	    vk::ImageLayout::eTransferDstOptimal,
	    blit,
	    vk::Filter::eLinear
	);

	// Only on the last face since GGX reaches across face boundaries
	if (face == 5) {
		prefilterInto(cmd, probe);
		convolveIrradianceInto(cmd, probe);
	}
}

void ReflectionProbePass::captureIrradianceFace(
    vk::CommandBuffer cmd, vk::Image scene_color, vk::Extent2D scene_extent, uint32_t face
) {
	if (face >= 6 || !m_staging.cube.isReady() || !isEnabled()) {
		return;
	}

	m_staging.cube.transition(
	    cmd, vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eTransfer
	);

	vk::ImageBlit blit {};
	blit.srcSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
	blit.srcOffsets[0] = vk::Offset3D {0, 0, 0};
	blit.srcOffsets[1] = vk::Offset3D {static_cast<int32_t>(scene_extent.width), static_cast<int32_t>(scene_extent.height), 1};
	blit.dstSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, face, 1);
	blit.dstOffsets[0] = vk::Offset3D {0, 0, 0};
	blit.dstOffsets[1] = vk::Offset3D {static_cast<int32_t>(m_staging.cube.size()), static_cast<int32_t>(m_staging.cube.size()), 1};

	cmd.blitImage(
	    scene_color,
	    vk::ImageLayout::eTransferSrcOptimal,
	    m_staging.cube.image(),
	    vk::ImageLayout::eTransferDstOptimal,
	    blit,
	    vk::Filter::eLinear
	);
}

void ReflectionProbePass::projectStagingToSh(vk::CommandBuffer cmd, uint32_t probe) {
	if (!m_sh_pipeline.isReady() || !m_sh_buffer.has_value() || *m_sh_set == VK_NULL_HANDLE) {
		return;
	}

	m_staging.cube.transition(
	    cmd, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead, vk::PipelineStageFlagBits::eComputeShader
	);

	struct ShParams {
		glm::uvec4 config {0};
	};

	const ShParams params {.config = glm::uvec4(VulkanRenderer::k_irradiance_capture_size, probe, 0, 0)};

	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_sh_pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eCompute, *m_sh_layout.getPipelineLayout(), 0, std::array<vk::DescriptorSet, 1> {*m_sh_set}, {}
	);
	cmd.pushConstants(*m_sh_layout.getPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, sizeof(ShParams), &params);
	cmd.dispatch(1, 1, 1);

	const vk::BufferMemoryBarrier barrier(
	    vk::AccessFlagBits::eShaderWrite,
	    vk::AccessFlagBits::eShaderRead,
	    VK_QUEUE_FAMILY_IGNORED,
	    VK_QUEUE_FAMILY_IGNORED,
	    **m_sh_buffer,
	    0,
	    VK_WHOLE_SIZE
	);
	cmd.pipelineBarrier(
	    vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader, {}, nullptr, barrier, nullptr
	);
}

auto ReflectionProbePass::getShBuffer() const -> vk::Buffer {
	return m_sh_buffer.has_value() ? **m_sh_buffer : vk::Buffer {};
}

void ReflectionProbePass::queueShSave(uint32_t base, uint32_t count, std::string_view uri, const ShGridKey& key) {
	m_pending_sh_saves.push_back({.base = base, .count = count, .uri = std::string(uri), .key = key});
}

namespace {

struct ShFileHeader {
	std::array<uint8_t, 4> magic = {'T', 'S', 'H', '1'};
	uint16_t version = 1;
	uint16_t padding = 0;
	uint32_t probe_count = 0;
	std::array<float, 3> min_corner {0.0f, 0.0f, 0.0f};
	std::array<float, 3> extents {0.0f, 0.0f, 0.0f};
	std::array<uint32_t, 3> counts {0, 0, 0};
};

auto gridMatches(const ShFileHeader& header, const ShGridKey& key) -> bool {
	constexpr float k_tolerance = 1e-3f;
	for (int i = 0; i < 3; ++i) {
		if (header.counts[static_cast<size_t>(i)] != key.counts[i]) {
			return false;
		}
		if (std::abs(header.min_corner[static_cast<size_t>(i)] - key.min_corner[i]) > k_tolerance) {
			return false;
		}
		if (std::abs(header.extents[static_cast<size_t>(i)] - key.extents[i]) > k_tolerance) {
			return false;
		}
	}
	return true;
}

}

auto ReflectionProbePass::loadShRange(uint32_t base, uint32_t count, std::string_view uri, const ShGridKey& key) -> bool {
	ZoneScoped;
	if (!m_sh_buffer.has_value() || count == 0 || m_core == nullptr) {
		return false;
	}

	const auto bytes = assets::AssetManager::get().loadBytes(uri);
	if (!bytes.has_value() || bytes->size() < sizeof(ShFileHeader)) {
		return false;
	}

	ShFileHeader header {};
	std::memcpy(&header, bytes->data(), sizeof(ShFileHeader));

	const ShFileHeader reference {};
	if (header.magic != reference.magic || header.version != reference.version) {
		TOAST_WARN("Render", "{} is not a irradiance cache this build can read; rebaking", uri);
		return false;
	}

	if (header.probe_count != count || !gridMatches(header, key)) {
		TOAST_INFO("Render", "{} was baked for a different grid; rebaking", uri);
		return false;
	}

	const size_t coefficient_bytes = static_cast<size_t>(count) * 4 * sizeof(glm::vec4);
	if (bytes->size() < sizeof(ShFileHeader) + coefficient_bytes) {
		TOAST_WARN("Render", "{} is truncated; rebaking", uri);
		return false;
	}

	vk::BufferCreateInfo staging_ci {};
	staging_ci.size = coefficient_bytes;
	staging_ci.usage = vk::BufferUsageFlagBits::eTransferSrc;
	staging_ci.sharingMode = vk::SharingMode::eExclusive;

	vma::AllocationCreateInfo staging_alloc {};
	staging_alloc.usage = vma::MemoryUsage::eAuto;
	staging_alloc.flags = vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped;

	auto staging = m_core->getAllocator().createBuffer(staging_ci, staging_alloc);
	auto* mapped = staging.getAllocation().getInfo().pMappedData;
	if (mapped == nullptr) {
		return false;
	}
	std::memcpy(mapped, bytes->data() + sizeof(ShFileHeader), coefficient_bytes);
	staging.getAllocation().flush(0, coefficient_bytes);

	const auto& device = m_core->getDevice();
	const vk::CommandPoolCreateInfo pool_ci(vk::CommandPoolCreateFlagBits::eTransient, m_core->getGraphicsQueueFamilyIndex());
	const vk::raii::CommandPool pool(device, pool_ci);
	const vk::CommandBufferAllocateInfo alloc_info(*pool, vk::CommandBufferLevel::ePrimary, 1);
	vk::raii::CommandBuffers buffers(device, alloc_info);
	auto& cmd = buffers.front();

	cmd.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
	const vk::BufferCopy copy(0, static_cast<vk::DeviceSize>(base) * 4 * sizeof(glm::vec4), coefficient_bytes);
	cmd.copyBuffer(*staging, **m_sh_buffer, copy);
	cmd.end();

	submitAndWait(device, m_core->getGraphicsQueue(), *cmd);

	TOAST_INFO("Render", "Loaded {} irradiance probe(s) from {}", count, uri);
	return true;
}

void ReflectionProbePass::createShResources(const VulkanCore& core) {
	ZoneScoped;
	const auto uid = assets::resolveURI("core://shaders/sh_project.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "sh_project.slang unavailable; irradiance volumes will not bake");
		return;
	}

	vk::BufferCreateInfo buffer_ci {};
	buffer_ci.size = static_cast<vk::DeviceSize>(VulkanRenderer::k_max_irradiance_probes) * 4 * sizeof(glm::vec4);
	buffer_ci.usage =
	    vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc;
	buffer_ci.sharingMode = vk::SharingMode::eExclusive;

	vma::AllocationCreateInfo allocation_ci {};
	allocation_ci.usage = vma::MemoryUsage::eAutoPreferDevice;

	m_sh_buffer.emplace(core.getAllocator().createBuffer(buffer_ci, allocation_ci));
	setDebugName(core, **m_sh_buffer, "ReflectionProbePass IrradianceSH");

	m_sh_layout.rebuild(core, shader->reflection, "ReflectionProbePass SH");

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::compute;
	config.debug_name = "ReflectionProbePass ShProject";
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_sh_layout.getPipelineLayout();
	// Slang renames the entry point of a single entry module to main
	config.compute_entry = "main";
	m_sh_pipeline.rebuild(core, config);

	const auto& layouts = m_sh_layout.getDescriptorSetLayouts();
	if (layouts.empty() || !m_staging.cube.isReady()) {
		return;
	}

	const vk::DescriptorSetLayout set_layout = *layouts[0];
	const vk::DescriptorSetAllocateInfo alloc_info(VulkanRenderer::instance->getDescriptorPoolHandle(), 1, &set_layout);
	auto allocated = core.getDevice().allocateDescriptorSets(alloc_info);
	m_sh_set = std::move(allocated[0]);
	setDebugName(core, *m_sh_set, "ReflectionProbePass ShSet");

	const vk::DescriptorImageInfo image_info(*m_sampler, m_staging.cube.cubeView(), vk::ImageLayout::eShaderReadOnlyOptimal);
	const vk::DescriptorBufferInfo buffer_info(**m_sh_buffer, 0, VK_WHOLE_SIZE);

	const std::array writes {
	  vk::WriteDescriptorSet(*m_sh_set, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &image_info),
	  vk::WriteDescriptorSet(*m_sh_set, 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &buffer_info)
	};
	core.getDevice().updateDescriptorSets(writes, {});
}

void ReflectionProbePass::renderFace(
    vk::CommandBuffer cmd, const VulkanPipeline& pipeline, const ProbeCube& target, uint32_t size, uint32_t mip, uint32_t face,
    float roughness
) {
	const auto basis = cubeFaceBasis(face);
	Params params {};
	params.face_right = glm::vec4(basis.right, 0.0f);
	params.face_up = glm::vec4(basis.up, 0.0f);
	params.face_forward = glm::vec4(basis.forward, 0.0f);
	params.roughness = roughness;
	params.intensity = 1.0f;

	vk::RenderingAttachmentInfo attachment {};
	attachment.imageView = target.cube.faceView(mip, face);
	attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
	attachment.loadOp = vk::AttachmentLoadOp::eDontCare;
	attachment.storeOp = vk::AttachmentStoreOp::eStore;

	const vk::Extent2D extent {size, size};
	vk::RenderingInfo rendering_info {};
	rendering_info.renderArea = vk::Rect2D({0, 0}, extent);
	rendering_info.layerCount = 1;
	rendering_info.colorAttachmentCount = 1;
	rendering_info.pColorAttachments = &attachment;

	cmd.beginRendering(rendering_info);
	const vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(size), static_cast<float>(size), 0.0f, 1.0f);
	cmd.setViewport(0, std::array {viewport});
	cmd.setScissor(0, std::array {vk::Rect2D({0, 0}, extent)});
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    *m_shader_layout.getPipelineLayout(),
	    0,
	    std::array<vk::DescriptorSet, 1> {*m_staging_source_set},
	    {}
	);
	cmd.pushConstants(*m_shader_layout.getPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, sizeof(Params), &params);
	cmd.draw(3, 1, 0, 0);
	cmd.endRendering();
}

void ReflectionProbePass::prefilterInto(vk::CommandBuffer cmd, uint32_t probe) {
	ZoneScoped;
	auto& cube = m_probes[probe];
	if (!cube.cube.isReady() || !m_prefilter_pipeline.isReady() || !*m_staging_source_set) {
		TOAST_WARN(
		    "Render",
		    "Probe {} prefilter skipped: image={} pipeline={} descriptor={}",
		    probe,
		    cube.cube.isReady(),
		    m_prefilter_pipeline.isReady(),
		    static_cast<bool>(*m_staging_source_set)
		);
		return;
	}

	m_staging.cube.transition(
	    cmd, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead, vk::PipelineStageFlagBits::eFragmentShader
	);
	cube.cube.transition(
	    cmd,
	    vk::ImageLayout::eColorAttachmentOptimal,
	    vk::AccessFlagBits::eColorAttachmentWrite,
	    vk::PipelineStageFlagBits::eColorAttachmentOutput
	);

	for (uint32_t mip = 0; mip < m_mip_levels; ++mip) {
		const uint32_t mip_size = std::max(cube.cube.size() >> mip, 1U);
		const float roughness = m_mip_levels > 1 ? static_cast<float>(mip) / static_cast<float>(m_mip_levels - 1) : 0.0f;
		for (uint32_t face = 0; face < 6; ++face) {
			renderFace(cmd, m_prefilter_pipeline, cube, mip_size, mip, face, roughness);
		}
	}

	cube.cube.transition(
	    cmd, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead, vk::PipelineStageFlagBits::eFragmentShader
	);
	cube.baked = true;
	TOAST_INFO("Render", "Probe {} baked", probe);
}

void ReflectionProbePass::convolveIrradianceInto(vk::CommandBuffer cmd, uint32_t probe) {
	ZoneScoped;
	auto& cube = m_probe_irradiance[probe];
	if (!cube.cube.isReady() || !m_irradiance_pipeline.isReady() || !*m_staging_source_set) {
		return;
	}

	cube.cube.transition(
	    cmd,
	    vk::ImageLayout::eColorAttachmentOptimal,
	    vk::AccessFlagBits::eColorAttachmentWrite,
	    vk::PipelineStageFlagBits::eColorAttachmentOutput
	);

	for (uint32_t face = 0; face < 6; ++face) {
		renderFace(cmd, m_irradiance_pipeline, cube, k_irradiance_size, 0, face, 0.0f);
	}

	cube.cube.transition(
	    cmd, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead, vk::PipelineStageFlagBits::eFragmentShader
	);
	cube.baked = true;
}

auto ReflectionProbePass::getProbeIrradianceView(uint32_t probe) const -> vk::ImageView {
	if (probe >= m_probe_irradiance.size() || !m_probe_irradiance[probe].cube.isReady()) {
		return {};
	}
	return m_probe_irradiance[probe].cube.cubeView();
}

void ReflectionProbePass::record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) {
	ZoneScoped;
	(void)frame_index;
	(void)image_index;

	// Must match WorkspaceViewModel RenderMode
	constexpr uint32_t k_probe_cubemap_mode = 11;

	if (!isEnabled() || !m_preview_pipeline.isReady() || m_preview_sets.empty()) {
		return;
	}

	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr || frame->render_mode != k_probe_cubemap_mode || frame->probe_capture_index >= 0) {
		return;
	}

	const vk::Extent2D extent {
	  static_cast<uint32_t>(frame->viewport_extent.x),
	  static_cast<uint32_t>(frame->viewport_extent.y),
	};
	if (extent.width == 0 || extent.height == 0) {
		return;
	}

	const vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f);
	cmd.setViewport(0, std::array {viewport});
	cmd.setScissor(0, std::array {vk::Rect2D({0, 0}, extent)});
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_preview_pipeline.getPipeline());

	for (uint32_t i = 0; i < frame->frame_data.reflection_probe_count_pad.x; ++i) {
		const auto& probe = frame->frame_data.reflection_probes[i];
		const auto cube = static_cast<uint32_t>(probe.params.x);
		if (cube >= m_preview_sets.size() || !isBaked(cube)) {
			continue;
		}

		const glm::vec3 extents(probe.box_extents_intensity);
		const float span = extents.x > 0.0f ? std::min({extents.x, extents.y, extents.z}) : probe.position_radius.w;

		PreviewParams params {};
		params.view_projection = frame->frame_data.view_projection;
		params.center_radius = glm::vec4(glm::vec3(probe.position_radius), std::max(span * 0.25f, 0.01f));
		params.camera_position = glm::vec4(frame->frame_data.camera_position, 0.0f);

		cmd.bindDescriptorSets(
		    vk::PipelineBindPoint::eGraphics,
		    *m_preview_layout.getPipelineLayout(),
		    0,
		    std::array<vk::DescriptorSet, 1> {*m_preview_sets[cube]},
		    {}
		);
		cmd.pushConstants(*m_preview_layout.getPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, sizeof(PreviewParams), &params);

		cmd.draw(24u * 48u * 6u, 1, 0, 0);
	}
}

auto ReflectionProbePass::cubeByteSize(const ProbeCube& cube) -> vk::DeviceSize {
	constexpr vk::DeviceSize k_bytes_per_texel = 8;

	vk::DeviceSize total = 0;
	for (uint32_t mip = 0; mip < cube.cube.mipLevels(); ++mip) {
		const vk::DeviceSize extent = std::max(cube.cube.size() >> mip, 1U);
		total += extent * extent * 6 * k_bytes_per_texel;
	}
	return total;
}

auto ReflectionProbePass::readbackCube(ProbeCube& cube, std::vector<uint8_t>& out) -> bool {
	ZoneScoped;
	if (!cube.cube.isReady() || m_core == nullptr) {
		return false;
	}

	const vk::DeviceSize bytes = cubeByteSize(cube);

	vk::BufferCreateInfo buffer_ci {};
	buffer_ci.size = bytes;
	buffer_ci.usage = vk::BufferUsageFlagBits::eTransferDst;
	buffer_ci.sharingMode = vk::SharingMode::eExclusive;

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAuto;
	alloc_ci.flags = vma::AllocationCreateFlagBits::eHostAccessRandom | vma::AllocationCreateFlagBits::eMapped;

	auto staging = m_core->getAllocator().createBuffer(buffer_ci, alloc_ci);

	const auto& device = m_core->getDevice();
	const vk::CommandPoolCreateInfo pool_ci(vk::CommandPoolCreateFlagBits::eTransient, m_core->getGraphicsQueueFamilyIndex());
	const vk::raii::CommandPool pool(device, pool_ci);
	auto buffers = device.allocateCommandBuffers(vk::CommandBufferAllocateInfo(*pool, vk::CommandBufferLevel::ePrimary, 1));
	const vk::raii::CommandBuffer cmd = std::move(buffers[0]);

	cmd.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

	const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, cube.cube.mipLevels(), 0, 6);
	vk::ImageMemoryBarrier to_src {};
	to_src.oldLayout = cube.cube.layout();
	to_src.newLayout = vk::ImageLayout::eTransferSrcOptimal;
	to_src.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	to_src.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	to_src.image = cube.cube.image();
	to_src.subresourceRange = range;
	to_src.srcAccessMask = vk::AccessFlagBits::eShaderRead;
	to_src.dstAccessMask = vk::AccessFlagBits::eTransferRead;
	cmd.pipelineBarrier(
	    vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eTransfer, {}, nullptr, nullptr, to_src
	);

	std::vector<vk::BufferImageCopy> regions;
	regions.reserve(cube.cube.mipLevels());
	vk::DeviceSize offset = 0;
	for (uint32_t mip = 0; mip < cube.cube.mipLevels(); ++mip) {
		const uint32_t extent = std::max(cube.cube.size() >> mip, 1U);
		vk::BufferImageCopy region {};
		region.bufferOffset = offset;
		region.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, mip, 0, 6);
		region.imageExtent = vk::Extent3D {extent, extent, 1};
		regions.push_back(region);
		offset += static_cast<vk::DeviceSize>(extent) * extent * 6 * 8;
	}
	cmd.copyImageToBuffer(cube.cube.image(), vk::ImageLayout::eTransferSrcOptimal, *staging, regions);

	vk::ImageMemoryBarrier back = to_src;
	back.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
	back.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	back.srcAccessMask = vk::AccessFlagBits::eTransferRead;
	back.dstAccessMask = vk::AccessFlagBits::eShaderRead;
	cmd.pipelineBarrier(
	    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, nullptr, nullptr, back
	);
	cube.cube.setLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

	cmd.end();

	submitAndWait(device, m_core->getGraphicsQueue(), *cmd);

	staging.getAllocation().invalidate(0, bytes);
	const auto* mapped = static_cast<const uint8_t*>(staging.getAllocation().getInfo().pMappedData);
	if (mapped == nullptr) {
		return false;
	}

	out.insert(out.end(), mapped, mapped + bytes);
	return true;
}

auto ReflectionProbePass::uploadCube(ProbeCube& cube, const std::vector<uint8_t>& bytes, size_t offset) -> bool {
	ZoneScoped;
	if (!cube.cube.isReady() || m_core == nullptr) {
		return false;
	}

	const vk::DeviceSize size = cubeByteSize(cube);
	if (offset + size > bytes.size()) {
		return false;
	}

	vk::BufferCreateInfo buffer_ci {};
	buffer_ci.size = size;
	buffer_ci.usage = vk::BufferUsageFlagBits::eTransferSrc;
	buffer_ci.sharingMode = vk::SharingMode::eExclusive;

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAuto;
	alloc_ci.flags = vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped;

	auto staging = m_core->getAllocator().createBuffer(buffer_ci, alloc_ci);
	auto* mapped = static_cast<uint8_t*>(staging.getAllocation().getInfo().pMappedData);
	if (mapped == nullptr) {
		return false;
	}
	std::memcpy(mapped, bytes.data() + offset, size);
	staging.getAllocation().flush(0, size);

	const auto& device = m_core->getDevice();
	const vk::CommandPoolCreateInfo pool_ci(vk::CommandPoolCreateFlagBits::eTransient, m_core->getGraphicsQueueFamilyIndex());
	const vk::raii::CommandPool pool(device, pool_ci);
	auto buffers = device.allocateCommandBuffers(vk::CommandBufferAllocateInfo(*pool, vk::CommandBufferLevel::ePrimary, 1));
	const vk::raii::CommandBuffer cmd = std::move(buffers[0]);

	cmd.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

	const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, cube.cube.mipLevels(), 0, 6);
	recordUndefinedToTransferDst(cmd, cube.cube.image(), range);

	std::vector<vk::BufferImageCopy> regions;
	regions.reserve(cube.cube.mipLevels());
	vk::DeviceSize local = 0;
	for (uint32_t mip = 0; mip < cube.cube.mipLevels(); ++mip) {
		const uint32_t extent = std::max(cube.cube.size() >> mip, 1U);
		vk::BufferImageCopy region {};
		region.bufferOffset = local;
		region.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, mip, 0, 6);
		region.imageExtent = vk::Extent3D {extent, extent, 1};
		regions.push_back(region);
		local += static_cast<vk::DeviceSize>(extent) * extent * 6 * 8;
	}
	cmd.copyBufferToImage(*staging, cube.cube.image(), vk::ImageLayout::eTransferDstOptimal, regions);

	recordTransferDstToShaderRead(cmd, cube.cube.image(), range);
	cube.cube.setLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

	cmd.end();

	submitAndWait(device, m_core->getGraphicsQueue(), *cmd);
	return true;
}

void ReflectionProbePass::queueSave(uint32_t probe, std::string_view uri) {
	if (probe >= m_probes.size() || uri.empty()) {
		return;
	}
	m_pending_saves.emplace_back(probe, std::string(uri));
}

void ReflectionProbePass::recordPre(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) {
	ZoneScoped;
	(void)frame_index;
	(void)image_index;

	// Bound cubemaps must not be eUndefined even when unbaked
	for (auto& probe : m_probes) {
		if (probe.cube.isReady()) {
			probe.cube.transition(
			    cmd,
			    vk::ImageLayout::eShaderReadOnlyOptimal,
			    vk::AccessFlagBits::eShaderRead,
			    vk::PipelineStageFlagBits::eFragmentShader
			);
		}
	}
	for (auto& irradiance : m_probe_irradiance) {
		if (irradiance.cube.isReady()) {
			irradiance.cube.transition(
			    cmd,
			    vk::ImageLayout::eShaderReadOnlyOptimal,
			    vk::AccessFlagBits::eShaderRead,
			    vk::PipelineStageFlagBits::eFragmentShader
			);
		}
	}
	if (m_staging.cube.isReady()) {
		m_staging.cube.transition(
		    cmd, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead, vk::PipelineStageFlagBits::eFragmentShader
		);
	}

	if ((m_pending_saves.empty() && m_pending_sh_saves.empty()) || m_core == nullptr) {
		return;
	}

	m_core->getDevice().waitIdle();

	for (const auto& [probe, uri] : m_pending_saves) {
		if (probe >= m_probes.size() || !m_probes[probe].baked) {
			continue;
		}

		ProbeFileHeader header {};
		header.face_size = m_probes[probe].cube.size();
		header.mip_levels = m_probes[probe].cube.mipLevels();
		header.irradiance_size = m_probe_irradiance[probe].cube.size();
		header.format = static_cast<uint32_t>(m_format);

		std::vector<uint8_t> bytes;
		bytes.reserve(sizeof(ProbeFileHeader) + cubeByteSize(m_probes[probe]) + cubeByteSize(m_probe_irradiance[probe]));
		const auto* header_bytes = reinterpret_cast<const uint8_t*>(&header);
		bytes.insert(bytes.end(), header_bytes, header_bytes + sizeof(ProbeFileHeader));

		if (!readbackCube(m_probes[probe], bytes) || !readbackCube(m_probe_irradiance[probe], bytes)) {
			TOAST_WARN("Render", "Probe {} could not be read back; not written to {}", probe, uri);
			continue;
		}

		if (assets::AssetManager::get().saveBytes(uri, bytes)) {
			TOAST_INFO("Render", "Probe {} written to {} ({} KiB)", probe, uri, bytes.size() / 1024);
		} else {
			TOAST_ERROR("Render", "Failed to write probe {} to {}", probe, uri);
		}
	}

	m_pending_saves.clear();

	for (const auto& save : m_pending_sh_saves) {
		if (!m_sh_buffer.has_value() || save.count == 0) {
			continue;
		}

		const size_t coefficient_bytes = static_cast<size_t>(save.count) * 4 * sizeof(glm::vec4);

		vk::BufferCreateInfo staging_ci {};
		staging_ci.size = coefficient_bytes;
		staging_ci.usage = vk::BufferUsageFlagBits::eTransferDst;
		staging_ci.sharingMode = vk::SharingMode::eExclusive;

		vma::AllocationCreateInfo staging_alloc {};
		staging_alloc.usage = vma::MemoryUsage::eAuto;
		staging_alloc.flags = vma::AllocationCreateFlagBits::eHostAccessRandom | vma::AllocationCreateFlagBits::eMapped;

		auto staging = m_core->getAllocator().createBuffer(staging_ci, staging_alloc);

		const auto& device = m_core->getDevice();
		const vk::CommandPoolCreateInfo pool_ci(vk::CommandPoolCreateFlagBits::eTransient, m_core->getGraphicsQueueFamilyIndex());
		const vk::raii::CommandPool pool(device, pool_ci);
		const vk::CommandBufferAllocateInfo alloc_info(*pool, vk::CommandBufferLevel::ePrimary, 1);
		vk::raii::CommandBuffers buffers(device, alloc_info);
		auto& readback = buffers.front();

		readback.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
		const vk::BufferCopy copy(static_cast<vk::DeviceSize>(save.base) * 4 * sizeof(glm::vec4), 0, coefficient_bytes);
		readback.copyBuffer(**m_sh_buffer, *staging, copy);
		readback.end();

		submitAndWait(device, m_core->getGraphicsQueue(), *readback);
		staging.getAllocation().invalidate(0, coefficient_bytes);

		ShFileHeader header {};
		header.probe_count = save.count;
		for (int i = 0; i < 3; ++i) {
			header.min_corner[static_cast<size_t>(i)] = save.key.min_corner[i];
			header.extents[static_cast<size_t>(i)] = save.key.extents[i];
			header.counts[static_cast<size_t>(i)] = save.key.counts[i];
		}

		const auto* mapped = static_cast<const uint8_t*>(staging.getAllocation().getInfo().pMappedData);
		if (mapped == nullptr) {
			TOAST_WARN("Render", "Irradiance coefficients could not be mapped; not written to {}", save.uri);
			continue;
		}

		std::vector<uint8_t> bytes;
		bytes.resize(sizeof(ShFileHeader) + coefficient_bytes);
		std::memcpy(bytes.data(), &header, sizeof(ShFileHeader));
		std::memcpy(bytes.data() + sizeof(ShFileHeader), mapped, coefficient_bytes);

		if (assets::AssetManager::get().saveBytes(save.uri, bytes)) {
			TOAST_INFO("Render", "{} irradiance probe(s) written to {} ({} KiB)", save.count, save.uri, bytes.size() / 1024);
		} else {
			TOAST_ERROR("Render", "Failed to write irradiance probes to {}", save.uri);
		}
	}

	m_pending_sh_saves.clear();
}

auto ReflectionProbePass::loadProbe(uint32_t probe, std::string_view uri) -> bool {
	ZoneScoped;
	if (probe >= m_probes.size() || m_core == nullptr) {
		return false;
	}

	const auto bytes = assets::AssetManager::get().tryLoadBytes(uri);
	if (!bytes.has_value() || bytes->size() < sizeof(ProbeFileHeader)) {
		return false;
	}

	ProbeFileHeader header {};
	std::memcpy(&header, bytes->data(), sizeof(ProbeFileHeader));

	const ProbeFileHeader reference {};
	if (header.magic != reference.magic || header.version != reference.version) {
		TOAST_WARN("Render", "{} is not a probe capture this build understands; it will be re-baked", uri);
		return false;
	}
	if (header.format != static_cast<uint32_t>(m_format) || header.mip_levels != m_mip_levels) {
		TOAST_WARN("Render", "{} was written for a different render target layout; it will be re-baked", uri);
		return false;
	}

	if (m_probes[probe].cube.size() != header.face_size) {
		m_core->getDevice().waitIdle();
		m_probes[probe].cube.create(
		    *m_core, m_format, header.face_size, m_mip_levels, std::format("ReflectionProbe[{}]", probe), k_probe_transfer_usage
		);
		rebindProbeViews(probe);
	}

	size_t offset = sizeof(ProbeFileHeader);
	if (!uploadCube(m_probes[probe], *bytes, offset)) {
		return false;
	}
	offset += cubeByteSize(m_probes[probe]);
	if (!uploadCube(m_probe_irradiance[probe], *bytes, offset)) {
		return false;
	}

	m_probes[probe].baked = true;
	m_probe_irradiance[probe].baked = true;
	TOAST_INFO("Render", "Probe {} loaded from {} at {}px", probe, uri, header.face_size);
	return true;
}

void ReflectionProbePass::setProbeResolution(uint32_t probe, uint32_t face_size) {
	ZoneScoped;
	if (probe >= m_probes.size() || m_core == nullptr) {
		return;
	}

	const uint32_t clamped = std::clamp(face_size, 32U, k_max_face_size);
	if (m_probes[probe].cube.size() == clamped) {
		return;
	}

	m_core->getDevice().waitIdle();

	m_probes[probe].cube.create(
	    *m_core, m_format, clamped, m_mip_levels, std::format("ReflectionProbe[{}]", probe), k_probe_transfer_usage
	);
	m_probes[probe].baked = false;

	rebindProbeViews(probe);

	TOAST_INFO("Render", "Probe {} resized to {}px; it needs re-baking", probe, clamped);
}

auto ReflectionProbePass::getProbeResolution(uint32_t probe) const -> uint32_t {
	return probe < m_probes.size() ? m_probes[probe].cube.size() : 0;
}

void ReflectionProbePass::rebindProbeViews(uint32_t probe) {
	if (probe >= m_preview_sets.size() || !m_probes[probe].cube.isReady()) {
		return;
	}

	vk::DescriptorImageInfo info {};
	info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	info.imageView = m_probes[probe].cube.cubeView();
	info.sampler = *m_sampler;

	const vk::WriteDescriptorSet write(*m_preview_sets[probe], 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &info);
	m_core->getDevice().updateDescriptorSets(write, {});

	VulkanRenderer::instance->requestMaterialFrameSetRebuild();
}

auto ReflectionProbePass::getProbeView(uint32_t probe) const -> vk::ImageView {
	if (probe >= m_probes.size() || !m_probes[probe].cube.isReady()) {
		return {};
	}
	return m_probes[probe].cube.cubeView();
}

auto ReflectionProbePass::isBaked(uint32_t probe) const -> bool {
	return probe < m_probes.size() && m_probes[probe].baked;
}

void ReflectionProbePass::setBakedTransform(uint32_t probe, const glm::vec3& position, const glm::vec3& extents) {
	if (probe >= m_probes.size()) {
		return;
	}
	m_probes[probe].baked_position = position;
	m_probes[probe].baked_extents = extents;
}

auto ReflectionProbePass::isStale(uint32_t probe, const glm::vec3& position, const glm::vec3& extents) const -> bool {
	if (probe >= m_probes.size() || !m_probes[probe].baked) {
		return true;
	}

	return m_probes[probe].baked_position != position || m_probes[probe].baked_extents != extents;
}

}
