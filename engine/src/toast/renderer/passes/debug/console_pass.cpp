#include "toast/renderer/passes/debug/console_pass.hpp"

#include "toast/renderer/core/vulkan_core.hpp"
#include "toast/renderer/vulkan_renderer.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <string>
#include <toast/assets/asset_manager.hpp>
#include <toast/console/console.hpp>
#include <toast/console/layout.hpp>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {
namespace {

constexpr double k_message_seconds = 2.0;

auto rectMin(const toast::console::Rect& r) -> ImVec2 {
	return {r.x, r.y};
}

auto rectMax(const toast::console::Rect& r) -> ImVec2 {
	return {r.x + r.w, r.y + r.h};
}

auto fitted(ImFont* font, float size, const std::string& text, float width) -> std::string {
	const auto measure = [&](const std::string& t) { return font->CalcTextSizeA(size, FLT_MAX, 0.0F, t.c_str()).x; };
	if (measure(text) <= width) {
		return text;
	}
	std::string cut = text;
	while (!cut.empty() && measure(cut + "...") > width) {
		cut.pop_back();
		while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0U) == 0x80U) {
			cut.pop_back();
		}
	}
	return cut + "...";
}

void draw(const toast::console::View& view, float width, float height) {
	using toast::console::EntryKind;

	ImFont* font = ImGui::GetFont();
	const float scale = std::max(1.0F, height / 900.0F);
	const float size = std::round(15.0F * scale);
	const float line = std::round(size * 1.3F);

	const auto measure = [&](std::string_view text) {
		return text.empty() ? 0.0F : font->CalcTextSizeA(size, FLT_MAX, 0.0F, text.data(), text.data() + text.size()).x;
	};
	const auto layout = toast::console::layout(view, width, height, line, measure);
	ImDrawList* list = ImGui::GetForegroundDrawList();
	const float pad = layout.padding;
	const auto text_at = [&](float x, float y, ImU32 c, const std::string& text) {
		list->AddText(font, size, ImVec2(x, y + ((line - size) * 0.5F)), c, text.c_str());
	};

	const ImU32 white = IM_COL32(255, 255, 255, 255);
	const ImU32 grey = IM_COL32(170, 170, 170, 255);
	const ImU32 strip = IM_COL32(0, 0, 0, 150);
	const ImU32 popup = IM_COL32(0, 0, 0, 190);
	const ImU32 selected = IM_COL32(255, 255, 255, 60);

	for (size_t i = 0; i < view.pins.size(); ++i) {
		const float y = static_cast<float>(i) * line;
		list->AddRectFilled(ImVec2(0.0F, y), ImVec2(measure(view.pins[i]) + (2.0F * pad), y + line), strip);
		text_at(pad, y, white, view.pins[i]);
	}

	if (!view.open) {
		if (!view.log.empty() && view.message_age < k_message_seconds) {
			const float y = height - line - pad;
			list->AddRectFilled(ImVec2(0.0F, y), ImVec2(width, y + line), strip);
			text_at(pad, y, white, view.log.back().second);
		}
		return;
	}

	// what the last lines said
	for (const auto& log : layout.log) {
		const auto& message = view.log[log.line].second;
		list->AddRectFilled(rectMin(log.rect), rectMax(log.rect), strip);
		text_at(log.rect.x + pad, log.rect.y, white, message);
	}

	if (!layout.rows.empty()) {
		list->AddRectFilled(rectMin(layout.popup), rectMax(layout.popup), popup);
		for (const auto& row : layout.rows) {
			const auto& entry = view.completion.entries[row.entry];
			if (row.entry == view.selected) {
				list->AddRectFilled(rectMin(row.rect), rectMax(row.rect), selected);
			}

			const std::string label =
			    entry.kind == EntryKind::group || entry.kind == EntryKind::subgroup ? entry.label + "." : entry.label;
			text_at(row.rect.x + pad, row.rect.y + (pad * 0.25F), white, label);

			if (!entry.value.empty()) {
				const float room = std::max(0.0F, row.rect.w - (3.0F * pad) - measure(label));
				const std::string shown = fitted(font, size, entry.value, room);
				text_at(row.rect.x + row.rect.w - pad - measure(shown), row.rect.y + (pad * 0.25F), grey, shown);
			}
		}
	}

	// the line itself
	list->AddRectFilled(rectMin(layout.bar), rectMax(layout.bar), strip);
	const float text_y = layout.bar.y + pad;
	text_at(pad, text_y, white, ">");
	text_at(pad + measure("> "), text_y, white, view.text);

	if (!view.completion.hint.empty()) {
		const float room = std::max(0.0F, layout.bar.w - layout.cursor_x - (4.0F * pad));
		const std::string shown = fitted(font, size, view.completion.hint, room);
		text_at(layout.bar.x + layout.bar.w - pad - measure(shown), text_y, grey, shown);
	}
	if (std::fmod(ImGui::GetTime(), 1.0) < 0.6) {
		list->AddRectFilled(
		    ImVec2(layout.cursor_x, text_y + 1.0F), ImVec2(layout.cursor_x + std::max(2.0F, scale), text_y + line - 1.0F), white
		);
	}
}

}

ConsolePass::ConsolePass(const renderer::VulkanCore& core, vk::Format color_format, vk::Format depth_format) {
	ZoneScoped;
	IMGUI_CHECKVERSION();
	ImGuiContext* previous = ImGui::GetCurrentContext();
	m_context = ImGui::CreateContext();
	ImGui::SetCurrentContext(m_context);
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.LogFilename = nullptr;
	io.BackendPlatformName = "toast_engine (console)";
	io.BackendRendererName = "imgui_impl_vulkan";
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	io.SetClipboardTextFn = nullptr;
	io.GetClipboardTextFn = nullptr;

	constexpr auto k_font = "core://ui/fonts/FiraCodeNerdFont-Regular.ttf";
	if (assets::AssetManager::isInitialized()) {
		if (auto bytes = assets::AssetManager::get().tryLoadBytes(k_font); bytes && !bytes->empty()) {
			m_font_data = std::move(*bytes);
			ImFontConfig config;
			config.FontDataOwnedByAtlas = false;
			if (io.Fonts->AddFontFromMemoryTTF(m_font_data.data(), static_cast<int>(m_font_data.size()), 0.0F, &config) == nullptr) {
				TOAST_WARN("Console", "Could not read the console font");
				m_font_data.clear();
			}
		} else {
			TOAST_WARN("Console", "{} is missing, using the built in font", k_font);
		}
	}

	const std::array<vk::Format, 1> color_formats {color_format};
	vk::PipelineRenderingCreateInfo rendering_ci {};
	rendering_ci.colorAttachmentCount = 1;
	rendering_ci.pColorAttachmentFormats = color_formats.data();
	rendering_ci.depthAttachmentFormat = depth_format;

	ImGui_ImplVulkan_InitInfo init_info {};
	init_info.ApiVersion = VK_API_VERSION_1_4;
	init_info.Instance = *core.getInstance();
	init_info.PhysicalDevice = *core.getPhysicalDevice();
	init_info.Device = *core.getDevice();
	init_info.QueueFamily = core.getGraphicsQueueFamilyIndex();
	init_info.Queue = core.getGraphicsQueue();
	init_info.DescriptorPoolSize = 8;
	init_info.MinImageCount = 2;
	init_info.ImageCount = VulkanRenderer::k_frames_in_flight;
	init_info.UseDynamicRendering = true;
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo = static_cast<VkPipelineRenderingCreateInfo>(rendering_ci);

	m_ready = ImGui_ImplVulkan_Init(&init_info);
	if (!m_ready) {
		TOAST_ERROR("Console", "Failed to initialize the ImGui Vulkan backend; the console will not be drawn");
		ImGui::DestroyContext(m_context);
		m_context = nullptr;
	}
	ImGui::SetCurrentContext(previous);
}

ConsolePass::~ConsolePass() {
	if (m_ready) {
		ImGui::SetCurrentContext(m_context);
		ImGui_ImplVulkan_Shutdown();
		ImGui::DestroyContext(m_context);
	}
}

void ConsolePass::update(uint32_t frame_index, float dt) {
	ZoneScoped;
	(void)frame_index;
	m_drawing = false;
	if (!m_ready) {
		return;
	}

	const toast::console::View view = toast::console::Console::get().view();
	const bool showing_message = !view.open && !view.log.empty() && view.message_age < k_message_seconds;
	if (!view.open && !showing_message && view.pins.empty()) {
		return;
	}
	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr || frame->output_extent.x < 1.0F || frame->output_extent.y < 1.0F) {
		return;
	}

	ImGui::SetCurrentContext(m_context);
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(frame->output_extent.x, frame->output_extent.y);
	io.DeltaTime = std::max(dt, 0.0001F);

	ImGui_ImplVulkan_NewFrame();
	ImGui::NewFrame();
	draw(view, frame->output_extent.x, frame->output_extent.y);
	ImGui::Render();
	m_drawing = true;
}

void ConsolePass::record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) {
	ZoneScoped;
	(void)frame_index;
	(void)image_index;
	if (!m_ready || !m_drawing) {
		return;
	}
	ImGui::SetCurrentContext(m_context);
	if (ImDrawData* draw_data = ImGui::GetDrawData(); draw_data != nullptr) {
		ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);
	}
}

}
