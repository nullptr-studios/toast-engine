#include "toast/renderer/passes/game/game_pass.hpp"

#include "toast/renderer/core/depth_convention.hpp"
#include "toast/renderer/passes/game/game_view_texture.hpp"
#include "toast/renderer/passes/render_pass_base.hpp"
#include "toast/renderer/shaders/shader_cache.hpp"
#include "toast/renderer/shaders/shader_layout.hpp"
#include "toast/renderer/shaders/vulkan_pipeline.hpp"
#include "toast/renderer/vulkan_renderer.hpp"

#include <cstring>
#include <format>
#include <map>
#include <mutex>
#include <string>
#include <toast/assets/assets.hpp>
#include <vector>

namespace renderer {

namespace {

constexpr vk::Format k_view_color_format = vk::Format::eR8G8B8A8Unorm;

}

/// Shared by the view pass, its draw passes and RmlUi textures
struct GameViewTarget {
	std::string name;
	vk::Extent2D extent;
	std::array<float, 4> clear_color {};
	std::optional<vma::raii::Image> color;
	std::optional<vk::raii::ImageView> color_view;
	sync::ImageState color_state;
	std::optional<vk::Format> depth_format;
	std::optional<vma::raii::Image> depth;
	std::optional<vk::raii::ImageView> depth_view;
	sync::ImageState depth_state;

	std::mutex mutex;
	std::vector<GamePass*> passes;
};

namespace {

std::mutex g_views_mutex;
std::map<std::string, std::shared_ptr<GameViewTarget>, std::less<>> g_views;

}

class GamePass final : public IRenderPass {
public:
	GamePass(const GamePassDesc& desc, std::shared_ptr<const ShaderCache::Entry> shader, std::shared_ptr<GameViewTarget> view)
	    : m_desc(desc),
	      m_name(desc.name != nullptr ? desc.name : "Game pass"),
	      m_shader(std::move(shader)),
	      m_view(std::move(view)) {
		m_desc.name = nullptr;
		m_desc.shader_uri = nullptr;
		m_desc.view = nullptr;
		m_pending.resize(static_cast<size_t>(m_desc.instance_stride) * m_desc.max_instances);
	}

	[[nodiscard]]
	auto stage() const -> RenderStage override {
		if (m_view) {
			return RenderStage::view;
		}
		return m_desc.stage == GamePassStage::overlay ? RenderStage::overlay : RenderStage::world;
	}

	[[nodiscard]]
	auto view() const -> const std::shared_ptr<GameViewTarget>& {
		return m_view;
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
		// The view records it inside its own scope
		if (!m_view) {
			draw(cmd, frame_index);
		}
	}

	void draw(vk::CommandBuffer cmd, uint32_t frame_index) {
		if (!m_pipeline.isReady() || frame_index >= m_sets.size()) {
			return;
		}

		uint32_t count = 0;
		{
			const std::lock_guard lock(m_mutex);
			count = m_count;
			const size_t bytes = static_cast<size_t>(count) * m_desc.instance_stride;
			if (bytes > 0) {
				const auto& allocation = m_instance_buffers[frame_index].getAllocation();
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

		const bool world = !m_view && m_desc.stage == GamePassStage::world;
		VulkanPipeline::Config config;
		config.debug_name = m_name;
		if (m_view) {
			config.color_format = k_view_color_format;
			config.depth_format = m_view->depth_format;
			config.extent = m_view->extent;
		} else {
			config.color_format = world ? renderer->getSceneColorFormat() : renderer->getOutputTarget().getColorFormat();
			config.depth_format = VulkanRenderer::selectDepthFormat(core);
			config.extent = renderer->renderExtent();
		}
		if (world) {
			config.extra_color_formats = worldStageExtraColorFormats();
			config.write_extra_color = m_desc.reactive && m_desc.blend != GamePassBlend::opaque;
			config.extra_blend_mask = config.write_extra_color ? 0b111u : 0u;
		}
		config.shader_spirv = m_shader->spirv;
		config.pipeline_layout = *m_layout.getPipelineLayout();
		config.cull_mode = vk::CullModeFlagBits::eNone;
		config.depth_test = m_desc.depth_test && config.depth_format.has_value();
		config.depth_write = m_desc.depth_write && config.depth_format.has_value();
		config.blend_enable = m_desc.blend != GamePassBlend::opaque;
		config.blend_preset = VulkanPipeline::BlendPreset::none;
		if (m_desc.blend == GamePassBlend::additive) {
			config.blend_preset = VulkanPipeline::BlendPreset::additive;
		} else if (m_desc.blend == GamePassBlend::alpha) {
			config.blend_preset = VulkanPipeline::BlendPreset::alpha;
		}
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
	std::shared_ptr<GameViewTarget> m_view;
	bool m_built = false;

	ShaderLayout m_layout;
	VulkanPipeline m_pipeline;
	std::vector<vk::raii::DescriptorSet> m_sets;
	std::vector<vma::raii::Buffer> m_instance_buffers;

	std::mutex m_mutex;
	std::vector<std::byte> m_pending;
	uint32_t m_count = 0;
};

class GameView final : public IRenderPass {
public:
	explicit GameView(std::shared_ptr<GameViewTarget> target) : m_target(std::move(target)) { }

	[[nodiscard]]
	auto stage() const -> RenderStage override {
		return RenderStage::view;
	}

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return m_target->name;
	}

	[[nodiscard]]
	auto target() const -> const std::shared_ptr<GameViewTarget>& {
		return m_target;
	}

	void record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override {
		(void)image_index;
		auto& target = *m_target;

		sync::BarrierBatch begin;
		begin.image(target.color_state, sync::Usage::color_attachment);
		if (target.depth) {
			begin.image(target.depth_state, sync::Usage::depth_attachment);
		}
		begin.flush(cmd);

		vk::RenderingAttachmentInfo color {};
		color.imageView = **target.color_view;
		color.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
		color.loadOp = vk::AttachmentLoadOp::eClear;
		color.storeOp = vk::AttachmentStoreOp::eStore;
		color.clearValue = vk::ClearValue(vk::ClearColorValue(target.clear_color));

		vk::RenderingAttachmentInfo depth {};
		if (target.depth) {
			depth.imageView = **target.depth_view;
			depth.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
			depth.loadOp = vk::AttachmentLoadOp::eClear;
			depth.storeOp = vk::AttachmentStoreOp::eDontCare;
			depth.clearValue = vk::ClearValue(vk::ClearDepthStencilValue(depth::k_far, 0));
		}

		vk::RenderingInfo info {};
		info.renderArea = vk::Rect2D({0, 0}, target.extent);
		info.layerCount = 1;
		info.colorAttachmentCount = 1;
		info.pColorAttachments = &color;
		info.pDepthAttachment = target.depth ? &depth : nullptr;

		cmd.beginRendering(info);
		cmd.setViewport(
		    0, vk::Viewport(0.0f, 0.0f, static_cast<float>(target.extent.width), static_cast<float>(target.extent.height), 0.0f, 1.0f)
		);
		cmd.setScissor(0, info.renderArea);
		{
			const std::lock_guard lock(target.mutex);
			for (GamePass* pass : target.passes) {
				if (pass->isEnabled()) {
					pass->draw(cmd, frame_index);
				}
			}
		}
		cmd.endRendering();

		sync::transition(cmd, target.color_state, sync::Usage::shader_sampled);
	}

private:
	std::shared_ptr<GameViewTarget> m_target;
};

auto createGameView(const GameViewDesc& desc) -> GameView* {
	if (VulkanRenderer::instance == nullptr || desc.name == nullptr || desc.width == 0 || desc.height == 0) {
		return nullptr;
	}
	const std::string_view name = desc.name;
	if (name.empty() || name.contains('/')) {
		TOAST_ERROR("Render", "Game view name '{}' must be non empty and without '/'", name);
		return nullptr;
	}

	const auto& core = VulkanRenderer::instance->getCore();
	auto target = std::make_shared<GameViewTarget>();
	target->name = name;
	target->extent = vk::Extent2D {desc.width, desc.height};
	std::ranges::copy(desc.clear_color, target->clear_color.begin());

	vma::AllocationCreateInfo allocation_ci {};
	allocation_ci.usage = vma::MemoryUsage::eAutoPreferDevice;

	target->color.emplace(
	    core.getAllocator().createImage(colorTargetImageInfo(target->extent, k_view_color_format), allocation_ci)
	);
	setDebugName(core, **target->color, std::format("GameView {} color", name));
	vk::ImageViewCreateInfo view_ci {};
	view_ci.image = **target->color;
	view_ci.viewType = vk::ImageViewType::e2D;
	view_ci.format = k_view_color_format;
	view_ci.subresourceRange = colorSubresourceRange();
	target->color_view.emplace(core.getDevice(), view_ci);
	target->color_state.reset(**target->color, colorSubresourceRange());

	if (desc.depth) {
		const vk::Format format = VulkanRenderer::selectDepthFormat(core);
		auto image_ci = colorTargetImageInfo(target->extent, format);
		image_ci.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
		target->depth.emplace(core.getAllocator().createImage(image_ci, allocation_ci));
		setDebugName(core, **target->depth, std::format("GameView {} depth", name));
		const vk::ImageSubresourceRange range {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1};
		view_ci.image = **target->depth;
		view_ci.format = format;
		view_ci.subresourceRange = range;
		target->depth_view.emplace(core.getDevice(), view_ci);
		target->depth_state.reset(**target->depth, range);
		target->depth_format = format;
	}

	const std::lock_guard lock(g_views_mutex);
	if (g_views.contains(name)) {
		TOAST_ERROR("Render", "Game view '{}' already exists", name);
		return nullptr;
	}
	auto pass = std::make_unique<GameView>(target);
	GameView* raw = pass.get();
	VulkanRenderer::instance->addRenderPass(std::move(pass));
	g_views.emplace(target->name, target);
	return raw;
}

void destroyGameView(GameView* view) {
	if (view == nullptr || VulkanRenderer::instance == nullptr) {
		return;
	}
	{
		const std::lock_guard lock(g_views_mutex);
		const auto it = g_views.find(view->target()->name);
		if (it != g_views.end() && it->second == view->target()) {
			g_views.erase(it);
		}
	}
	VulkanRenderer::instance->removeRenderPass(view);
}

auto findGameViewTexture(std::string_view name) -> std::optional<GameViewTexture> {
	const std::lock_guard lock(g_views_mutex);
	const auto it = g_views.find(name);
	if (it == g_views.end()) {
		return std::nullopt;
	}
	return GameViewTexture {.view = **it->second->color_view, .extent = it->second->extent, .keepalive = it->second};
}

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
	auto view = desc.view != nullptr ? desc.view->target() : nullptr;
	auto pass = std::make_unique<GamePass>(desc, std::move(shader), view);
	GamePass* raw = pass.get();
	VulkanRenderer::instance->addRenderPass(std::move(pass));
	if (view) {
		const std::lock_guard lock(view->mutex);
		view->passes.push_back(raw);
	}
	return raw;
}

void setGamePassInstances(GamePass* pass, const void* data, uint32_t count) {
	if (pass != nullptr) {
		pass->setInstances(data, count);
	}
}

void destroyGamePass(GamePass* pass) {
	if (pass != nullptr && VulkanRenderer::instance != nullptr) {
		if (const auto& view = pass->view()) {
			const std::lock_guard lock(view->mutex);
			std::erase(view->passes, pass);
		}
		VulkanRenderer::instance->removeRenderPass(pass);
	}
}

}
