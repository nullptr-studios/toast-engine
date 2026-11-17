#include "toast/renderer/passes/game/game_pass.hpp"

#include "toast/renderer/passes/render_pass_base.hpp"
#include "toast/renderer/shaders/shader_cache.hpp"
#include "toast/renderer/shaders/shader_layout.hpp"
#include "toast/renderer/shaders/vulkan_pipeline.hpp"
#include "toast/renderer/vulkan_renderer.hpp"

#include <cstring>
#include <format>
#include <mutex>
#include <string>
#include <toast/assets/assets.hpp>
#include <vector>

namespace renderer {

class GamePass final : public IRenderPass {
public:
	GamePass(const GamePassDesc& desc, std::shared_ptr<const ShaderCache::Entry> shader)
	    : m_desc(desc),
	      m_name(desc.name != nullptr ? desc.name : "Game pass"),
	      m_shader(std::move(shader)) {
		m_desc.name = nullptr;
		m_desc.shader_uri = nullptr;
		m_pending.resize(static_cast<size_t>(m_desc.instance_stride) * m_desc.max_instances);
	}

	[[nodiscard]]
	auto stage() const -> RenderStage override {
		return m_desc.stage == GamePassStage::overlay ? RenderStage::overlay : RenderStage::world;
	}

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return m_name;
	}

	void setInstances(const void* data, uint32_t count) {
		count = std::min(count, m_desc.max_instances);
		const std::lock_guard lock(m_mutex);
		if (count > 0 && data != nullptr) {
			std::memcpy(m_pending.data(), data, static_cast<size_t>(count) * m_desc.instance_stride);
		}
		m_count = data != nullptr ? count : 0;
	}

	void update(uint32_t frame_index, float dt) override {
		(void)frame_index;
		(void)dt;
		if (!m_built) {
			m_built = true;
			build();
		}
	}

	void record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override {
		(void)image_index;
		if (!m_pipeline.isReady() || frame_index >= m_sets.size()) {
			return;
		}

		uint32_t count = 0;
		{
			const std::lock_guard lock(m_mutex);
			count = m_count;
			const size_t bytes = static_cast<size_t>(count) * m_desc.instance_stride;
			if (bytes > 0) {
				auto& allocation = m_instance_buffers[frame_index].getAllocation();
				std::memcpy(allocation.getInfo().pMappedData, m_pending.data(), bytes);
				allocation.flush(0, bytes);
			}
		}
		if (count == 0) {
			return;
		}

		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline.getPipeline());
		cmd.bindDescriptorSets(
		    vk::PipelineBindPoint::eGraphics, *m_layout.getPipelineLayout(), 0, std::array {*m_sets[frame_index]}, {}
		);
		cmd.draw(m_desc.vertices_per_instance, count, 0, 0);
	}

private:
	void build() {
		auto* renderer = VulkanRenderer::instance;
		const auto& core = renderer->getCore();
		const auto& device = core.getDevice();

		m_layout.rebuild(core, m_shader->reflection, m_name);
		const auto& set_layouts = m_layout.getDescriptorSetLayouts();
		if (set_layouts.empty()) {
			TOAST_ERROR("Render", "Game pass '{}' shader declares no descriptor set", m_name);
			return;
		}

		const bool world = m_desc.stage == GamePassStage::world;
		VulkanPipeline::Config config;
		config.debug_name = m_name;
		config.color_format = world ? renderer->getSceneColorFormat() : renderer->getOutputTarget().getColorFormat();
		if (world) {
			config.extra_color_formats = worldStageExtraColorFormats();
			config.write_extra_color = m_desc.reactive && m_desc.blend != GamePassBlend::opaque;
			config.extra_blend_mask = config.write_extra_color ? 0b111u : 0u;
		}
		config.depth_format = VulkanRenderer::selectDepthFormat(core);
		config.extent = renderer->renderExtent();
		config.shader_spirv = m_shader->spirv;
		config.pipeline_layout = *m_layout.getPipelineLayout();
		config.cull_mode = vk::CullModeFlagBits::eNone;
		config.depth_test = m_desc.depth_test;
		config.depth_write = m_desc.depth_write;
		config.blend_enable = m_desc.blend != GamePassBlend::opaque;
		config.blend_preset = m_desc.blend == GamePassBlend::additive ? VulkanPipeline::BlendPreset::additive
		                      : m_desc.blend == GamePassBlend::alpha  ? VulkanPipeline::BlendPreset::alpha
		                                                              : VulkanPipeline::BlendPreset::none;
		m_pipeline.rebuild(core, config);
		if (!m_pipeline.isReady()) {
			TOAST_ERROR("Render", "Game pass '{}' pipeline failed to build", m_name);
			return;
		}

		const vk::DeviceSize buffer_size = std::max<vk::DeviceSize>(m_pending.size(), 16);
		const vk::DescriptorSetLayout set_layout = *set_layouts[0];
		const vk::DescriptorPool pool = renderer->getDescriptorPoolHandle();
		for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
			vk::BufferCreateInfo buffer_ci {};
			buffer_ci.size = buffer_size;
			buffer_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer;
			vma::AllocationCreateInfo alloc_ci {};
			alloc_ci.usage = vma::MemoryUsage::eAuto;
			alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;
			m_instance_buffers.push_back(core.getAllocator().createBuffer(buffer_ci, alloc_ci));
			setDebugName(core, *m_instance_buffers.back(), std::format("{} instances[{}]", m_name, i));

			auto allocated = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &set_layout));
			m_sets.push_back(std::move(allocated[0]));

			const auto* frame_res = renderer->getFrameUBORes(i);
			const vk::DescriptorBufferInfo camera_info(**frame_res->gpu_buffer, 0, sizeof(FrameUBO));
			const vk::DescriptorBufferInfo instance_info(*m_instance_buffers.back(), 0, buffer_size);
			const std::array writes {
			  vk::WriteDescriptorSet(*m_sets.back(), 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &camera_info),
			  vk::WriteDescriptorSet(*m_sets.back(), 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &instance_info),
			};
			device.updateDescriptorSets(writes, {});
		}
		TOAST_INFO("Render", "Game pass '{}' ready", m_name);
	}

	GamePassDesc m_desc;
	std::string m_name;
	std::shared_ptr<const ShaderCache::Entry> m_shader;
	bool m_built = false;

	ShaderLayout m_layout;
	VulkanPipeline m_pipeline;
	std::vector<vk::raii::DescriptorSet> m_sets;
	std::vector<vma::raii::Buffer> m_instance_buffers;

	std::mutex m_mutex;
	std::vector<std::byte> m_pending;
	uint32_t m_count = 0;
};

auto createGamePass(const GamePassDesc& desc) -> GamePass* {
	if (VulkanRenderer::instance == nullptr || desc.shader_uri == nullptr || desc.instance_stride == 0) {
		return nullptr;
	}
	const auto uid = assets::resolveURI(desc.shader_uri);
	auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "Game pass shader {} unavailable", desc.shader_uri);
		return nullptr;
	}
	auto pass = std::make_unique<GamePass>(desc, std::move(shader));
	GamePass* raw = pass.get();
	VulkanRenderer::instance->addRenderPass(std::move(pass));
	return raw;
}

void setGamePassInstances(GamePass* pass, const void* data, uint32_t count) {
	if (pass != nullptr) {
		pass->setInstances(data, count);
	}
}

void destroyGamePass(GamePass* pass) {
	if (pass != nullptr && VulkanRenderer::instance != nullptr) {
		VulkanRenderer::instance->removeRenderPass(pass);
	}
}

}
