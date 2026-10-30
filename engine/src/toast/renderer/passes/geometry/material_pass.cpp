#include "toast/renderer/passes/geometry/material_pass.hpp"

#include "toast/renderer/core/descriptor_writer.hpp"
#include "toast/renderer/core/vulkan_core.hpp"
#include "toast/renderer/core/vulkan_debug.hpp"
#include "toast/renderer/passes/lighting/cluster_lighting_pass.hpp"
#include "toast/renderer/passes/lighting/environment_pass.hpp"
#include "toast/renderer/passes/lighting/reflection_probe_pass.hpp"
#include "toast/renderer/passes/lighting/shadow_pass.hpp"
#include "toast/renderer/raytracing/ray_tracing_scene.hpp"
#include "toast/renderer/resources/vulkan_texture.hpp"
#include "toast/renderer/vulkan_renderer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <format>
#include <mutex>
#include <ranges>
#include <string>
#include <toast/assets/texture.hpp>
#include <toast/log.hpp>
#include <toast/thread_pool.hpp>
#include <tracy/Tracy.hpp>
#include <unordered_set>

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

void warnIfWrongColorSpace(const MaterialRuntime::TextureSlot& slot, const VulkanTexture& texture) {
	if (!slot.linear_data) {
		return;
	}

	switch (texture.getFormat()) {
		case vk::Format::eR8G8B8A8Srgb:
		case vk::Format::eB8G8R8A8Srgb:
		case vk::Format::eBc7SrgbBlock:
		case vk::Format::eBc1RgbaSrgbBlock:
		case vk::Format::eBc3SrgbBlock:
		case vk::Format::eAstc4x4SrgbBlock: break;
		default: return;
	}

	static std::mutex warned_mutex;
	static std::unordered_set<VkImageView> warned;

	const std::lock_guard lock(warned_mutex);
	if (!warned.emplace(static_cast<VkImageView>(texture.getView())).second) {
		return;
	}

	TOAST_WARN(
	    "Render",
	    "Texture bound to a linear material slot is sRGB-encoded ({}); it will sample wrongly. Reimport it - "
	    "the glTF/texture importer now tags normal, metallic, roughness and occlusion maps as linear",
	    vk::to_string(texture.getFormat())
	);
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
      m_root_material_ref(root_material),
      m_name(root_material != nullptr ? root_material->name() : "Material"),
      m_color_format(color_format),
      m_depth_format(depth_format),
      m_extent(extent) {
	startBuild();
}

MaterialPass::~MaterialPass() {
	// The job owns copies of its inputs but must not outlive the core
	if (m_pending_build.valid()) {
		m_pending_build.wait();
	}
}

auto MaterialPass::resolvedAlphaCutoffOf(MaterialRuntime& runtime) -> float {
	const auto& blobs = runtime.uniformBlobs();
	for (const auto& binding : runtime.reflection().bindings) {
		if (binding.kind != ShaderBindingKind::uniform_buffer || !binding.engine_semantic.empty()) {
			continue;
		}
		const auto blob =
		    std::ranges::find_if(blobs, [&](const auto& b) { return b.set == binding.set && b.binding == binding.binding; });
		if (blob == blobs.end()) {
			continue;
		}
		for (const auto& member : binding.members) {
			if (member.name != "alphaCutoff" || member.type != ShaderMemberType::float_t ||
			    member.offset + sizeof(float) > blob->bytes.size()) {
				continue;
			}
			float value = 0.0f;
			std::memcpy(&value, blob->bytes.data() + member.offset, sizeof(float));
			return value;
		}
	}
	return 0.0f;
}

auto MaterialPass::build(const BuildInputs& inputs) -> std::shared_ptr<Built> {
	ZoneScopedN("MaterialPass build");
	try {
		auto built = std::make_shared<Built>(*inputs.core, inputs.root_material);
		const auto& entries = built->runtime.shaderEntries();
		if (entries.empty()) {
			TOAST_WARN("Render", "MaterialPass '{}' has no compiled shaders, nothing will be drawn", inputs.name);
			return nullptr;
		}
		if (entries.size() > 1) {
			TOAST_WARN(
			    "Render", "MaterialPass '{}': multiple shader modules per material not supported yet, using the first", inputs.name
			);
		}

		built->layout.rebuild(*inputs.core, built->runtime.reflection(), inputs.name);

		const assets::MaterialSettings settings = inputs.root_material->settings();

		VulkanPipeline::Config config;
		config.pipeline_type = VulkanPipeline::PipelineType::graphics;
		config.debug_name = std::format("MaterialPass ({})", inputs.name);
		config.color_format = inputs.color_format;
		config.depth_format = inputs.depth_format;
		config.extent = inputs.extent;
		config.shader_spirv = entries.front()->spirv;
		config.pipeline_layout = *built->layout.getPipelineLayout();
		config.vertex_bindings = {vertexBindingDescription()};
		const auto vertex_attributes = vertexAttributeDescriptions();
		config.vertex_attributes.assign(vertex_attributes.begin(), vertex_attributes.end());
		config.depth_test = settings.depth_test;
		config.depth_write = settings.depth_write;

		// Or equal since the prepass writes the exact same depth
		config.depth_compare = depth::k_closer_or_equal;
		config.cull_mode = toCullMode(settings.cull_mode);
		config.blend_preset = toBlendPreset(settings.blend_mode);
		config.extra_color_formats = worldStageExtraColorFormats();

		// The cutout entry point discards which costs early Z
		built->uses_cutout = resolvedAlphaCutoffOf(built->runtime) > 0.0f;
		if (built->uses_cutout) {
			config.fragment_entry = "fragmentMainCutout";
		}

		config.write_extra_color = config.blend_preset == VulkanPipeline::BlendPreset::none;

		built->pipeline.rebuild(*inputs.core, config);
		if (!built->pipeline.isReady()) {
			TOAST_ERROR("Render", "MaterialPass '{}' pipeline failed to build", inputs.name);
			return nullptr;
		}

		const auto& blobs = built->runtime.uniformBlobs();
		for (const auto& binding : built->runtime.reflection().bindings) {
			if (binding.kind != ShaderBindingKind::uniform_buffer || !binding.engine_semantic.empty()) {
				continue;
			}
			const auto blob =
			    std::ranges::find_if(blobs, [&](const auto& b) { return b.set == binding.set && b.binding == binding.binding; });
			if (blob == blobs.end()) {
				continue;
			}

			std::string values;
			for (const auto& member : binding.members) {
				if (!member.inspector.reflected || member.type != ShaderMemberType::float_t ||
				    member.offset + sizeof(float) > blob->bytes.size()) {
					continue;
				}
				float value = 0.0f;
				std::memcpy(&value, blob->bytes.data() + member.offset, sizeof(float));
				values += std::format("{}{}={:.3f}", values.empty() ? "" : ", ", member.name, value);
			}
			if (!values.empty()) {
				TOAST_TRACE("Render", "MaterialPass '{}' resolved scalars: {}", inputs.name, values);
			}
		}

		return built;
	} catch (const std::exception& e) {
		TOAST_ERROR("Render", "MaterialPass '{}' build threw: {}", inputs.name, e.what());
		return nullptr;
	}
}

void MaterialPass::startBuild() {
	m_rebuild_pending.store(false, std::memory_order_release);
	BuildInputs inputs {
	  .core = m_core,
	  .root_material = m_root_material,
	  .name = m_name,
	  .color_format = m_color_format,
	  .depth_format = m_depth_format,
	  .extent = m_extent,
	};
	m_pending_build = toast::ThreadPool::push([inputs = std::move(inputs)] { return build(inputs); });
}

void MaterialPass::adopt(std::shared_ptr<Built> built) {
	ZoneScoped;
	if (built == nullptr) {
		if (m_built != nullptr) {
			TOAST_WARN("Render", "MaterialPass '{}' rebuild failed, keeping the previous pipeline", m_name);
		}
		return;
	}

	const auto& set_layouts = built->layout.getDescriptorSetLayouts();
	if (!set_layouts.empty()) {
		built->scene_sets.create(*m_core, built->runtime.reflection(), *set_layouts[0], m_name);
	}

	// Frames in flight may still bind the old pipeline and its sets
	if (m_built != nullptr) {
		VulkanRenderer::instance->retire(std::move(m_built));
	}
	m_built = std::move(built);
	TOAST_INFO("Render", "MaterialPass '{}' pipeline ready", m_name);
}

void MaterialPass::update(uint32_t frame_index, float dt) {
	(void)frame_index;
	(void)dt;

	if (m_pending_build.valid() && m_pending_build.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		adopt(m_pending_build.get());
	}

	if (m_pending_build.valid()) {
		return;
	}

	if (m_rebuild_pending.load(std::memory_order_acquire)) {
		startBuild();
		return;
	}

	// Kept while a build runs since that build may have baked the values before they changed
	if (m_built != nullptr && m_values_dirty.exchange(false, std::memory_order_acq_rel)) {
		for (auto& [material, res] : m_built->instances) {
			res.runtime->markValuesDirty();
		}
	}
}

void MaterialPass::finishPendingBuild() {
	if (m_pending_build.valid()) {
		ZoneScopedN("MaterialPass wait for build");
		adopt(m_pending_build.get());
	}
}

auto MaterialPass::ensureInstanceResources(Built& built, assets::Material* material) -> InstanceResources* {
	ZoneScoped;
	auto [it, inserted] = built.instances.try_emplace(material);
	InstanceResources& res = it->second;
	if (!inserted) {
		return &res;
	}

	res.runtime = std::make_unique<MaterialRuntime>(*m_core, material);

	// The variant comes from the root material but alphaCutoff is per instance
	if (!built.uses_cutout && resolvedAlphaCutoffOf(*res.runtime) > 0.0f) {
		TOAST_WARN(
		    "Render",
		    "Material instance '{}' sets alphaCutoff but its root material '{}' does not, so this pass built the "
		    "pipeline without the alpha test - the cutout will not apply. Set alphaCutoff on the root material",
		    material != nullptr ? material->name() : "<null>",
		    m_name
		);
	}

	const auto& layouts = built.layout.getDescriptorSetLayouts();
	if (layouts.size() < 2) {
		return &res;
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
		res.bound_views[frame].assign(res.runtime->textureSlots().size(), InstanceResources::BoundView {});

		DescriptorWriter writer;
		for (const auto& ubo : res.ubo_buffers) {
			if (ubo.set == 0 || ubo.set > material_set_count) {
				continue;
			}
			writer.buffer(*res.sets[frame][ubo.set - 1], ubo.binding, vk::DescriptorType::eUniformBuffer, **ubo.buffers[frame]);
		}
		writer.flush(device);
	}

	return &res;
}

void MaterialPass::updateInstanceDescriptors(InstanceResources& res, uint32_t frame_index) {
	ZoneScoped;
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

	const auto& slots = res.runtime->textureSlots();
	if (res.bound_views[frame_index].size() != slots.size()) {
		res.bound_views[frame_index].assign(slots.size(), InstanceResources::BoundView {});
	}

	for (size_t i = 0; i < slots.size(); ++i) {
		const auto& slot = slots[i];
		if (slot.set == 0 || slot.set > res.sets[frame_index].size()) {
			continue;
		}

		vk::ImageView view;
		if (slot.default_fallback == "black") {
			view = VulkanRenderer::instance->getDefaultBlackTextureView();
		} else if (slot.default_fallback == "flat_normal") {
			view = VulkanRenderer::instance->getDefaultNormalTextureView();
		} else {
			view = VulkanRenderer::instance->getDefaultTextureView();
		}
		vk::Sampler sampler = slot.sampler ? slot.sampler : VulkanRenderer::instance->getDefaultSampler();

		const VulkanTexture* gpu_texture = slot.texture.hasValue() ? &slot.texture->gpuTexture() : nullptr;
		uint64_t generation = 0;
		if (gpu_texture != nullptr && gpu_texture->isReady() && gpu_texture->getView()) {
			view = gpu_texture->getView();
			generation = gpu_texture->generation();
			warnIfWrongColorSpace(slot, *gpu_texture);
		} else if (
		    const vk::ImageView failsafe =
		        VulkanRenderer::instance->getFailsafeTextureView(slot.texture.uid().data() != 0, gpu_texture)
		) {
			view = failsafe;
			sampler = VulkanRenderer::instance->getFailsafeSampler();
		}

		const InstanceResources::BoundView bound {.view = view, .generation = generation};
		if (res.bound_views[frame_index][i] == bound) {
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
		res.bound_views[frame_index][i] = bound;
	}
}

void MaterialPass::record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) {
	ZoneScoped;
	(void)image_index;

	if (m_built == nullptr || !m_built->scene_sets.get(frame_index)) {
		return;
	}
	Built& built = *m_built;

	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr) {
		return;
	}

	built.scene_sets.updateTlas(frame_index);

	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    *built.layout.getPipelineLayout(),
	    0,
	    std::array<vk::DescriptorSet, 1> {built.scene_sets.get(frame_index)},
	    {}
	);

	assets::Material* bound_material = nullptr;

	const vk::Buffer posed_vertices = VulkanRenderer::instance->getPosedVertexBuffer(frame_index);
	const auto posed_offset_of = [&](const MeshInstanceProxy& proxy) {
		return posed_vertices ? proxy.posed_vertex_offset : MeshInstanceProxy::k_no_posed_vertices;
	};

	const auto draw_instance = [&](const MeshInstanceProxy& proxy, uint32_t instance_count = 1) {
		drawInstance(built, cmd, frame_index, proxy, posed_vertices, posed_offset_of(proxy), &bound_material, instance_count);
	};

	const auto range =
	    std::ranges::find_if(frame->material_ranges, [this](const auto& r) { return r.root_material == m_root_material; });
	if (range == frame->material_ranges.end()) {
		return;
	}

	std::vector<uint32_t>& order = m_draw_order;
	order.clear();
	order.reserve(range->end - range->begin);
	for (uint32_t i = range->begin; i < range->end; ++i) {
		if (frame->mesh_instances[i].visible) {
			order.push_back(i);
		}
	}
	if (order.empty()) {
		return;
	}

	if (isBlended()) {
		const glm::vec3 camera_position = frame->frame_data.camera_position;
		const auto distance_squared = [&](uint32_t index) {
			const glm::vec3 origin = glm::vec3(frame->mesh_instances[index].model[3]);
			const glm::vec3 delta = origin - camera_position;
			return glm::dot(delta, delta);
		};
		std::ranges::sort(order, [&](uint32_t a, uint32_t b) { return distance_squared(a) > distance_squared(b); });
	}

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, built.pipeline.getPipeline());

	const bool allow_batching = !isBlended() && built.runtime.instanceBaseOffset().has_value();

	for (size_t i = 0; i < order.size();) {
		const auto& proxy = frame->mesh_instances[order[i]];
		if (proxy.mesh == nullptr || !proxy.mesh->isReady() || proxy.root_material != m_root_material) {
			++i;
			continue;
		}

		const bool posed = posed_offset_of(proxy) != MeshInstanceProxy::k_no_posed_vertices;

		uint32_t run = 1;
		if (allow_batching && !posed) {
			while (i + run < order.size()) {
				const auto& next = frame->mesh_instances[order[i + run]];
				if (next.mesh != proxy.mesh || next.material != proxy.material || next.root_material != m_root_material ||
				    posed_offset_of(next) != MeshInstanceProxy::k_no_posed_vertices ||
				    next.instance_index != proxy.instance_index + run) {
					break;
				}
				++run;
			}
		}

		draw_instance(proxy, run);
		i += run;
	}
}

void MaterialPass::drawInstance(
    Built& built, vk::CommandBuffer cmd, uint32_t frame_index, const MeshInstanceProxy& proxy, vk::Buffer posed_vertices,
    uint32_t posed_vertex_offset, assets::Material** bound_material, uint32_t instance_count
) {
	InstanceResources* res = ensureInstanceResources(built, proxy.material != nullptr ? proxy.material : m_root_material);
	if (res == nullptr || res->runtime == nullptr) {
		return;
	}

	if (bound_material == nullptr || *bound_material != res->runtime->material()) {
		updateInstanceDescriptors(*res, frame_index);

		if (!res->sets[frame_index].empty()) {
			std::vector<vk::DescriptorSet> raw_sets;
			raw_sets.reserve(res->sets[frame_index].size());
			for (const auto& set : res->sets[frame_index]) {
				raw_sets.push_back(*set);
			}
			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *built.layout.getPipelineLayout(), 1, raw_sets, {});
		}
		if (bound_material != nullptr) {
			*bound_material = res->runtime->material();
		}
	}

	const auto& blob = res->runtime->pushBlob();
	m_push_scratch.assign(blob.begin(), blob.end());
	std::vector<std::byte>& push_data = m_push_scratch;
	if (!push_data.empty()) {
		if (const auto instance_base_offset = res->runtime->instanceBaseOffset();
		    instance_base_offset.has_value() && *instance_base_offset + sizeof(uint32_t) <= push_data.size()) {
			std::memcpy(push_data.data() + *instance_base_offset, &proxy.instance_index, sizeof(uint32_t));
		}
		if (const auto model_offset = res->runtime->modelOffset();
		    model_offset.has_value() && *model_offset + sizeof(glm::mat4) <= push_data.size()) {
			std::memcpy(push_data.data() + *model_offset, &proxy.model, sizeof(glm::mat4));
		}

		if (const auto joint_offset_offset = res->runtime->jointOffsetOffset();
		    joint_offset_offset.has_value() && *joint_offset_offset + sizeof(uint32_t) <= push_data.size()) {
			std::memcpy(push_data.data() + *joint_offset_offset, &proxy.joint_offset, sizeof(uint32_t));
		}
		cmd.pushConstants(
		    *built.layout.getPipelineLayout(),
		    vk::ShaderStageFlagBits::eAll,
		    0,
		    static_cast<uint32_t>(push_data.size()),
		    push_data.data()
		);
	}

	if (posed_vertices && posed_vertex_offset != MeshInstanceProxy::k_no_posed_vertices) {
		proxy.mesh->bindPosed(cmd, posed_vertices, posed_vertex_offset);
	} else {
		proxy.mesh->bind(cmd);
	}
	proxy.mesh->draw(cmd, instance_count);
}

void MaterialPass::recordInstance(vk::CommandBuffer cmd, uint32_t frame_index, const MeshInstanceProxy& proxy) {
	if (m_built == nullptr || !m_built->scene_sets.get(frame_index) || proxy.mesh == nullptr || !proxy.mesh->isReady()) {
		return;
	}
	Built& built = *m_built;

	built.scene_sets.updateTlas(frame_index);

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, built.pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    *built.layout.getPipelineLayout(),
	    0,
	    std::array<vk::DescriptorSet, 1> {built.scene_sets.get(frame_index)},
	    {}
	);

	const vk::Buffer posed_vertices = VulkanRenderer::instance->getPosedVertexBuffer(frame_index);
	drawInstance(built, cmd, frame_index, proxy, posed_vertices, proxy.posed_vertex_offset, nullptr);
}

}
