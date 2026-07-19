#include "material_pass.hpp"

#include "../vulkan_core.hpp"
#include "../vulkan_debug.hpp"
#include "../vulkan_renderer.hpp"
#include "../vulkan_texture.hpp"

#include <array>
#include <cstring>
#include <format>
#include <toast/assets/texture.hpp>
#include <toast/log.hpp>

namespace renderer {

namespace {

auto toBlendPreset(assets::BlendMode mode) -> VulkanPipeline::BlendPreset {
	switch (mode) {
		case assets::BlendMode::alpha: return VulkanPipeline::BlendPreset::alpha;
		case assets::BlendMode::additive: return VulkanPipeline::BlendPreset::additive;
		case assets::BlendMode::multiply: return VulkanPipeline::BlendPreset::multiply;
		default: return VulkanPipeline::BlendPreset::none;
	}
}

auto toCullMode(assets::CullMode mode) -> vk::CullModeFlags {
	switch (mode) {
		case assets::CullMode::none: return vk::CullModeFlagBits::eNone;
		case assets::CullMode::front: return vk::CullModeFlagBits::eFront;
		default: return vk::CullModeFlagBits::eBack;
	}
}

}

MaterialPass::MaterialPass(
    const VulkanCore& core, assets::Material* root_material, vk::Format color_format, vk::Format depth_format, vk::Extent2D extent
)
    : m_core(&core),
      m_root_material(root_material),
      m_name(root_material != nullptr ? root_material->name() : "Material"),
      m_color_format(color_format),
      m_depth_format(depth_format),
      m_extent(extent),
      m_root_runtime(core, root_material) {
	rebuildPipeline();
}

void MaterialPass::rebuildPipeline() {
	m_instances.clear();
	m_frame_descriptor_sets.clear();
	m_pipeline.reset();

	m_root_runtime.rebuild();
	const auto& entries = m_root_runtime.shaderEntries();
	if (entries.empty()) {
		TOAST_WARN("Render", "MaterialPass '{}' has no compiled shaders, nothing will be drawn", m_name);
		return;
	}
	if (entries.size() > 1) {
		TOAST_WARN("Render", "MaterialPass '{}': multiple shader modules per material not supported yet, using the first", m_name);
	}

	m_layout.rebuild(*m_core, m_root_runtime.reflection(), m_name);

	const assets::MaterialSettings settings = m_root_material->settings();

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.debug_name = std::format("MaterialPass ({})", m_name);
	config.color_format = m_color_format;
	config.depth_format = m_depth_format;
	config.extent = m_extent;
	config.shader_spirv = entries.front()->spirv;
	config.pipeline_layout = *m_layout.getPipelineLayout();
	config.vertex_binding = vertexBindingDescription();
	const auto vertex_attributes = vertexAttributeDescriptions();
	config.vertex_attributes.assign(vertex_attributes.begin(), vertex_attributes.end());
	config.depth_test = settings.depth_test;
	config.depth_write = settings.depth_write;
	config.cull_mode = toCullMode(settings.cull_mode);
	config.blend_preset = toBlendPreset(settings.blend_mode);

	m_pipeline.rebuild(*m_core, config);
	createFrameSets();
}

void MaterialPass::createFrameSets() {
	const auto& layouts = m_layout.getDescriptorSetLayouts();
	if (layouts.empty()) {
		return;
	}

	const auto& device = m_core->getDevice();
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();
	const vk::DescriptorSetLayout frame_set_layout = *layouts[0];

	m_frame_descriptor_sets.clear();
	m_frame_descriptor_sets.reserve(VulkanRenderer::k_frames_in_flight);

	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		const vk::DescriptorSetAllocateInfo alloc_info(pool, 1, &frame_set_layout);
		auto allocated = device.allocateDescriptorSets(alloc_info);
		m_frame_descriptor_sets.push_back(std::move(allocated[0]));
		setDebugName(*m_core, *m_frame_descriptor_sets[i], std::format("{} FrameSet[{}]", m_name, i));

		const auto* frame_res = VulkanRenderer::instance->getFrameUBORes(i);
		if (!frame_res->gpu_buffer.has_value()) {
			TOAST_CRITICAL("Render", "Frame UBO buffer missing for frame {}", i);
			continue;
		}

		const vk::DescriptorBufferInfo buffer_info(**frame_res->gpu_buffer, 0, sizeof(VulkanRenderer::FrameUBO));
		const vk::WriteDescriptorSet write(
		    *m_frame_descriptor_sets[i], 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &buffer_info
		);
		device.updateDescriptorSets(write, {});
	}
}

auto MaterialPass::ensureInstanceResources(assets::Material* material) -> InstanceResources* {
	auto [it, inserted] = m_instances.try_emplace(material);
	InstanceResources& res = it->second;
	if (!inserted) {
		return &res;
	}

	res.runtime = std::make_unique<MaterialRuntime>(*m_core, material);

	const auto& layouts = m_layout.getDescriptorSetLayouts();
	if (layouts.size() < 2) {
		return &res;    // shader has no material sets
	}

	const auto& device = m_core->getDevice();
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();
	const size_t material_set_count = layouts.size() - 1;

	for (const auto& blob : res.runtime->uniformBlobs()) {
		InstanceResources::UboBuffer ubo;
		ubo.set = blob.set;
		ubo.binding = blob.binding;
		ubo.buffers.resize(VulkanRenderer::k_frames_in_flight);

		for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
			vk::BufferCreateInfo buffer_ci {};
			buffer_ci.size = std::max<vk::DeviceSize>(blob.bytes.size(), 16);
			buffer_ci.usage = vk::BufferUsageFlagBits::eUniformBuffer;

			vma::AllocationCreateInfo alloc_ci {};
			alloc_ci.usage = vma::MemoryUsage::eAutoPreferHost;
			alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

			ubo.buffers[i].emplace(m_core->getAllocator().createBuffer(buffer_ci, alloc_ci));
			setDebugName(*m_core, **ubo.buffers[i], std::format("{} MaterialUBO set{} b{} frame{}", m_name, blob.set, blob.binding, i));
		}
		res.ubo_buffers.push_back(std::move(ubo));
	}

	res.sets.resize(VulkanRenderer::k_frames_in_flight);
	res.bound_views.resize(VulkanRenderer::k_frames_in_flight);
	for (uint32_t frame = 0; frame < VulkanRenderer::k_frames_in_flight; ++frame) {
		res.sets[frame].reserve(material_set_count);
		for (size_t set_index = 1; set_index < layouts.size(); ++set_index) {
			const vk::DescriptorSetLayout layout = *layouts[set_index];
			const vk::DescriptorSetAllocateInfo alloc_info(pool, 1, &layout);
			auto allocated = device.allocateDescriptorSets(alloc_info);
			res.sets[frame].push_back(std::move(allocated[0]));
			setDebugName(
			    *m_core,
			    *res.sets[frame].back(),
			    std::format("{} MaterialSet{} frame{} ({})", m_name, set_index, frame, material->name())
			);
		}
		res.bound_views[frame].assign(res.runtime->textureSlots().size(), vk::ImageView {});

		std::vector<vk::DescriptorBufferInfo> buffer_infos;
		std::vector<vk::WriteDescriptorSet> writes;
		buffer_infos.reserve(res.ubo_buffers.size());
		for (const auto& ubo : res.ubo_buffers) {
			if (ubo.set == 0 || ubo.set > material_set_count) {
				continue;
			}
			buffer_infos.emplace_back(**ubo.buffers[frame], 0, VK_WHOLE_SIZE);
			writes.emplace_back(
			    *res.sets[frame][ubo.set - 1], ubo.binding, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &buffer_infos.back()
			);
		}
		if (!writes.empty()) {
			device.updateDescriptorSets(writes, {});
		}
	}

	return &res;
}

void MaterialPass::updateInstanceDescriptors(InstanceResources& res, uint32_t frame_index) {
	const auto& device = m_core->getDevice();
	const auto& blobs = res.runtime->uniformBlobs();
	for (const auto& blob : blobs) {
		for (auto& ubo : res.ubo_buffers) {
			if (ubo.set != blob.set || ubo.binding != blob.binding || !ubo.buffers[frame_index].has_value()) {
				continue;
			}
			const auto& allocation = ubo.buffers[frame_index]->getAllocation();
			if (auto* mapped = allocation.getInfo().pMappedData) {
				std::memcpy(mapped, blob.bytes.data(), blob.bytes.size());
				allocation.flush(0, blob.bytes.size());
			}
		}
	}

	// Texture descriptors
	const auto& slots = res.runtime->textureSlots();
	if (res.bound_views[frame_index].size() != slots.size()) {
		res.bound_views[frame_index].assign(slots.size(), vk::ImageView {});
	}

	for (size_t i = 0; i < slots.size(); ++i) {
		const auto& slot = slots[i];
		if (slot.set == 0 || slot.set > res.sets[frame_index].size()) {
			continue;
		}

		vk::ImageView view = VulkanRenderer::instance->getDefaultTextureView();
		vk::Sampler sampler = slot.sampler ? slot.sampler : VulkanRenderer::instance->getDefaultSampler();
		if (slot.texture.hasValue()) {
			const auto& gpu_texture = slot.texture->gpuTexture();
			if (gpu_texture.isReady() && gpu_texture.getView()) {
				view = gpu_texture.getView();
			}
		}

		if (res.bound_views[frame_index][i] == view) {
			continue;
		}

		vk::DescriptorImageInfo image_info {};
		image_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		image_info.imageView = view;
		image_info.sampler = sampler;

		const vk::WriteDescriptorSet write(
		    *res.sets[frame_index][slot.set - 1], slot.binding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &image_info
		);
		device.updateDescriptorSets(write, {});
		res.bound_views[frame_index][i] = view;
	}
}

void MaterialPass::record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) {
	(void)image_index;

	// Structural rebuild
	if (m_rebuild_pending.exchange(false, std::memory_order_acq_rel)) {
		m_core->getDevice().waitIdle();
		rebuildPipeline();
	}

	if (m_values_dirty.exchange(false, std::memory_order_acq_rel)) {
		for (auto& [material, res] : m_instances) {
			res.runtime->markValuesDirty();
		}
	}

	if (!m_pipeline.isReady() || m_frame_descriptor_sets.size() != VulkanRenderer::k_frames_in_flight) {
		return;
	}

	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr) {
		return;
	}

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    *m_layout.getPipelineLayout(),
	    0,
	    std::array<vk::DescriptorSet, 1> {*m_frame_descriptor_sets[frame_index]},
	    {}
	);

	assets::Material* bound_material = nullptr;
	std::vector<std::byte> push_data;

	for (const auto& proxy : frame->mesh_instances) {
		if (proxy.mesh == nullptr || !proxy.mesh->isReady() || proxy.root_material != m_root_material) {
			continue;
		}

		InstanceResources* res = ensureInstanceResources(proxy.material != nullptr ? proxy.material : m_root_material);
		if (res == nullptr || res->runtime == nullptr) {
			continue;
		}

		if (bound_material != res->runtime->material()) {
			updateInstanceDescriptors(*res, frame_index);

			if (!res->sets[frame_index].empty()) {
				std::vector<vk::DescriptorSet> raw_sets;
				raw_sets.reserve(res->sets[frame_index].size());
				for (const auto& set : res->sets[frame_index]) {
					raw_sets.push_back(*set);
				}
				cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_layout.getPipelineLayout(), 1, raw_sets, {});
			}
			bound_material = res->runtime->material();
		}

		// Push constants
		push_data = res->runtime->pushBlob();
		if (!push_data.empty()) {
			if (const auto model_offset = res->runtime->modelOffset();
			    model_offset.has_value() && *model_offset + sizeof(glm::mat4) <= push_data.size()) {
				std::memcpy(push_data.data() + *model_offset, &proxy.model, sizeof(glm::mat4));
			}
			cmd.pushConstants(
			    *m_layout.getPipelineLayout(),
			    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
			    0,
			    static_cast<uint32_t>(push_data.size()),
			    push_data.data()
			);
		}

		proxy.mesh->bind(cmd);
		proxy.mesh->draw(cmd);
	}
}

}
