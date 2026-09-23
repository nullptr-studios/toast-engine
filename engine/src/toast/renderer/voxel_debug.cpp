#include "voxel_debug.hpp"

#include "passes/voxel_pass.hpp"
#include "voxel_gpu_storage.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <limits>
#include <string>
#include <toast/voxel/gpu_layout.hpp>
#include <tracy/Tracy.hpp>

namespace renderer::voxel_debug {

namespace {

using Proxy = VulkanRenderer::VoxelVolumeProxy;

constexpr auto k_upload_window = std::chrono::seconds(5);

/// Uploads inside the window that read as continuous re-uploading
constexpr size_t k_upload_churn = 10;

constexpr ImVec4 k_warn_color {1.0f, 0.7f, 0.2f, 1.0f};

[[nodiscard]]
auto voxelDebugImColor(Rgb color, float alpha = 1.0f) -> ImVec4 {
	return {color.r, color.g, color.b, alpha};
}

[[nodiscard]]
auto voxelDebugViewName(View view) -> const char* {
	switch (view) {
		case View::steps: return "DDA steps";
		case View::traversal: return "Traversal levels";
		case View::bricks: return "Brick storage";
		case View::volumes: return "Volumes";
		case View::materials: return "Physical materials";
	}
	return "Unknown";
}

[[nodiscard]]
auto voxelDebugTagName(uint32_t entry) -> const char* {
	switch (static_cast<voxel::BrickTag>(entry & 3u)) {
		case voxel::BrickTag::empty: return "empty";
		case voxel::BrickTag::uniform: return "uniform";
		case voxel::BrickTag::shared: return "shared";
		case voxel::BrickTag::owned: return "owned";
	}
	return "?";
}

[[nodiscard]]
auto voxelDebugBytes(uint64_t bytes) -> std::string {
	if (bytes >= 1024ull * 1024ull) {
		return std::format("{:.2f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
	}
	if (bytes >= 1024ull) {
		return std::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
	}
	return std::format("{} B", bytes);
}

[[nodiscard]]
auto voxelDebugNodeName(const VulkanRenderer::RenderFrame& frame, uint32_t record) -> std::string_view {
	if (!frame.voxel_storage) {
		return "?";
	}
	const auto& names = frame.voxel_storage->debugInfo().node_names;
	return record < names.size() && !names[record].empty() ? std::string_view(names[record]) : std::string_view("(unnamed)");
}

[[nodiscard]]
auto voxelDebugToWorld(const Proxy& proxy) -> glm::mat4 {
	return proxy.model * glm::scale(glm::mat4(1.0f), glm::vec3(voxel::k_voxel_size));
}

/// Dummy not ColorButton so repeated swatches never share an ImGui id
void voxelDebugSwatch(Rgb color, const char* label) {
	const float size = ImGui::GetTextLineHeight();
	const ImVec2 corner = ImGui::GetCursorScreenPos();
	ImGui::GetWindowDrawList()->AddRectFilled(
	    corner, ImVec2(corner.x + size, corner.y + size), ImGui::ColorConvertFloat4ToU32(voxelDebugImColor(color))
	);
	ImGui::Dummy(ImVec2(size, size));
	ImGui::SameLine();
	ImGui::TextUnformatted(label);
}

/// Edges with an endpoint behind the camera are skipped rather than clipped
void voxelDebugOutline(
    ImDrawList* list, const VulkanRenderer::RenderFrame& frame, const glm::mat4& voxel_to_world, glm::vec3 min, glm::vec3 max,
    ImU32 color, float thickness
) {
	const glm::mat4 to_clip = frame.frame_data.view_projection * voxel_to_world;
	std::array<glm::vec4, 8> clip {};
	for (uint32_t i = 0; i < clip.size(); ++i) {
		const glm::vec3 corner((i & 1u) != 0 ? max.x : min.x, (i & 2u) != 0 ? max.y : min.y, (i & 4u) != 0 ? max.z : min.z);
		clip[i] = to_clip * glm::vec4(corner, 1.0f);
	}

	constexpr std::array<std::array<uint32_t, 2>, 12> k_edges {
	  {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}
	};
	const auto screen = [&](const glm::vec4& point) {
		const glm::vec2 ndc = glm::vec2(point) / point.w;
		return ImVec2((ndc.x * 0.5f + 0.5f) * frame.viewport_extent.x, (ndc.y * 0.5f + 0.5f) * frame.viewport_extent.y);
	};
	for (const auto& [a, b] : k_edges) {
		if (clip[a].w <= 1.0e-4f || clip[b].w <= 1.0e-4f) {
			continue;
		}
		list->AddLine(screen(clip[a]), screen(clip[b]), color, thickness);
	}
}

void voxelDebugLegend(View view) {
	switch (view) {
		case View::steps: {
			for (size_t i = 0; i < k_step_band_colors.size(); ++i) {
				const std::string label = i + 1 < k_step_band_starts.size()
				                              ? std::format("{} - {} steps", k_step_band_starts[i], k_step_band_starts[i + 1] - 1)
				                              : std::format("{}+ steps", k_step_band_starts[i]);
				voxelDebugSwatch(k_step_band_colors[i], label.c_str());
			}
			voxelDebugSwatch(k_exhausted_color, std::format("hit the {} step cap unresolved", voxel::k_max_march_steps).c_str());
			ImGui::TextDisabled("Hatched: box pixels whose ray missed, still paid for");
			break;
		}
		case View::traversal:
			voxelDebugSwatch({1.0f, 0.0f, 0.0f}, "single voxel steps in non empty bricks");
			voxelDebugSwatch({0.0f, 1.0f, 0.0f}, "empty bricks skipped 8 voxels at a time");
			voxelDebugSwatch({0.0f, 0.0f, 1.0f}, "empty coarse cells skipped 32 at a time");
			ImGui::TextDisabled("Colours mix by share of the ray's steps. Hatched: miss");
			break;
		case View::bricks:
			voxelDebugSwatch(k_uniform_brick_color, "uniform: solid one material no pool storage");
			voxelDebugSwatch(k_shared_brick_color, "shared: pooled and still the asset's copy");
			voxelDebugSwatch(k_owned_brick_color, "owned: copied on write by this instance");
			ImGui::TextDisabled("Dark lines bricks, white coarse cells, faint voxels");
			break;
		case View::volumes:
			ImGui::TextDisabled("One hue per volume record");
			ImGui::TextDisabled("Hatched: box pixels whose ray missed, the raster overdraw");
			break;
		case View::materials:
			voxelDebugSwatch(k_default_material_color, "material 0: the default and every entry without one");
			ImGui::TextDisabled("Any other hue is one physical material id");
			break;
	}
}

void voxelDebugProbeText(const VulkanRenderer::RenderFrame& frame, const std::optional<ProbeResult>& result) {
	if (!frame.voxel_storage) {
		ImGui::TextDisabled("No voxel storage uploaded");
		return;
	}
	if (!frame.voxel_storage->debugInfo().mirror) {
		ImGui::TextDisabled("Cursor probe starts once the debug upload lands");
		return;
	}
	if (!result.has_value()) {
		ImGui::TextDisabled("Cursor outside the viewport");
		return;
	}

	if (result->hit.has_value()) {
		const Probe& probe = *result->hit;
		const glm::ivec3 voxel = probe.hit.voxel;
		const glm::ivec3 brick = voxel / static_cast<int32_t>(voxel::k_brick_dim);
		const glm::ivec3 local = voxel - brick * static_cast<int32_t>(voxel::k_brick_dim);
		const glm::ivec3 coarse = brick / static_cast<int32_t>(voxel::gpu::k_coarse_bricks);
		const std::string_view name = voxelDebugNodeName(frame, probe.proxy.record_index);
		const voxel::PaletteEntry& entry = probe.palette_entry;

		ImGui::Text(
		    "'%.*s'  record %u  uid %016llx",
		    static_cast<int>(name.size()),
		    name.data(),
		    probe.proxy.record_index,
		    static_cast<unsigned long long>(probe.proxy.node_uid)
		);
		ImGui::Text(
		    "voxel (%d, %d, %d)  brick (%d, %d, %d)  local (%d, %d, %d)  coarse (%d, %d, %d)",
		    voxel.x,
		    voxel.y,
		    voxel.z,
		    brick.x,
		    brick.y,
		    brick.z,
		    local.x,
		    local.y,
		    local.z,
		    coarse.x,
		    coarse.y,
		    coarse.z
		);

		const uint32_t payload = probe.brick_entry >> voxel::k_brick_tag_bits;
		const bool uniform = (probe.brick_entry & 3u) == static_cast<uint32_t>(voxel::BrickTag::uniform);
		ImGui::Text("brick %s, %s %u", voxelDebugTagName(probe.brick_entry), uniform ? "palette" : "pool slot", payload);

		ImGui::Text(
		    "palette %u  material %u  #%02X%02X%02X  rough %.2f  metal %.2f  emissive %.2f%s",
		    static_cast<uint32_t>(probe.hit.material),
		    static_cast<uint32_t>(entry.material),
		    entry.albedo_r,
		    entry.albedo_g,
		    entry.albedo_b,
		    static_cast<float>(entry.roughness) / 255.0f,
		    static_cast<float>(entry.metallic) / 255.0f,
		    static_cast<float>(entry.emissive) / 255.0f * probe.max_emissive,
		    (entry.flags & voxel::k_entry_transparent) != 0 ? "  transparent" : ""
		);

		const uint32_t voxel_steps = probe.hit.steps - probe.hit.coarse_steps - probe.hit.brick_steps;
		ImGui::Text(
		    "%u steps: %u coarse skips, %u brick skips, %u voxel",
		    probe.hit.steps,
		    probe.hit.coarse_steps,
		    probe.hit.brick_steps,
		    voxel_steps
		);

		std::string face = "started inside a solid voxel";
		if (probe.hit.axis >= 0) {
			const bool positive = probe.face[probe.hit.axis] > 0;
			face = std::format("{}{} face", positive ? '+' : '-', "XYZ"[probe.hit.axis]);
		}
		ImGui::Text(
		    "%.2f m away, %s%s%s",
		    probe.distance,
		    face.c_str(),
		    probe.proxy.camera_inside ? ", camera inside the box" : "",
		    probe.proxy.mirrored ? ", mirrored" : ""
		);
	} else {
		ImGui::TextDisabled("No voxel under the cursor");
	}

	if (result->miss.has_value()) {
		const MissProbe& miss = *result->miss;
		const std::string_view name = voxelDebugNodeName(frame, miss.proxy.record_index);
		ImGui::TextDisabled(
		    "Missed '%.*s' after %u steps (%u coarse, %u brick)",
		    static_cast<int>(name.size()),
		    name.data(),
		    miss.hit.steps,
		    miss.hit.coarse_steps,
		    miss.hit.brick_steps
		);
	}
}

}

auto probe(const VulkanRenderer::RenderFrame& frame, glm::vec2 cursor) -> std::optional<ProbeResult> {
	ZoneScoped;
	const auto& storage = frame.voxel_storage;
	if (!storage || !storage->isReady() || !storage->debugInfo().mirror) {
		return std::nullopt;
	}

	const glm::vec2 extent = frame.viewport_extent;
	if (cursor.x < 0.0f || cursor.y < 0.0f || cursor.x >= extent.x || cursor.y >= extent.y) {
		return std::nullopt;
	}

	const VoxelPackedScene& packed = *storage->debugInfo().mirror;

	// Vulkan NDC y points down like pixel rows
	const glm::vec2 ndc = ((cursor + 0.5f) / extent) * 2.0f - 1.0f;
	const glm::mat4 inverse_view_projection = glm::inverse(frame.frame_data.view_projection);
	const auto unproject = [&](float depth) {
		const glm::vec4 point = inverse_view_projection * glm::vec4(ndc, depth, 1.0f);
		return glm::vec3(point) / point.w;
	};
	const glm::vec3 near_point = unproject(0.0f);

	// Half depth since an infinite projection puts the far plane at w = 0
	const glm::vec3 direction = unproject(0.5f) - near_point;

	const glm::mat4 inverse_voxel_scale = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / voxel::k_voxel_size));

	ProbeResult result;
	uint32_t drawn = 0;
	for (const Proxy& proxy : frame.voxel_instances) {
		if (!proxy.visible) {
			continue;
		}
		// Same volumes and cap as VoxelPass::record
		if (drawn++ >= VoxelPass::k_max_instances) {
			break;
		}
		if (proxy.record_index >= packed.scene.records.size()) {
			continue;
		}

		const glm::mat4 world_to_voxel = inverse_voxel_scale * proxy.inverse_model;
		const glm::vec3 origin(world_to_voxel * glm::vec4(near_point, 1.0f));
		const glm::vec3 voxel_direction(world_to_voxel * glm::vec4(direction, 0.0f));

		// One t across volumes since an affine map keeps the ray parameter
		const voxel::RayHit hit = voxel::marchRay(
		    packed.pool, packed.scene, proxy.record_index, origin, voxel_direction, 0.0f, std::numeric_limits<float>::max()
		);
		if (!hit.hit) {
			if (hit.steps > 0 && (!result.miss.has_value() || hit.steps > result.miss->hit.steps)) {
				result.miss = MissProbe {.proxy = proxy, .hit = hit};
			}
			continue;
		}
		if (result.hit.has_value() && hit.t >= result.hit->hit.t) {
			continue;
		}

		const voxel::gpu::VolumeRecord& record = packed.scene.records[proxy.record_index];
		const glm::uvec3 dims(record.brick_dims_x, record.brick_dims_y, record.brick_dims_z);
		const glm::uvec3 brick(hit.voxel / static_cast<int32_t>(voxel::k_brick_dim));

		Probe out {.proxy = proxy, .hit = hit};
		out.world_position = near_point + direction * hit.t;
		out.distance = glm::length(out.world_position - frame.frame_data.camera_position);
		out.brick_entry = packed.scene.grids[record.grid_offset + voxel::gpu::gridIndex(dims, brick)];
		out.max_emissive = record.max_emissive;
		if (hit.axis >= 0) {
			out.face[hit.axis] = voxel_direction[hit.axis] > 0.0f ? -1 : 1;
		}

		const size_t palette_word = (static_cast<size_t>(record.palette_offset) + hit.material) * 4;
		if (palette_word + 4 <= packed.scene.palettes.size()) {
			std::memcpy(&out.palette_entry, packed.scene.palettes.data() + palette_word, sizeof(voxel::PaletteEntry));
		}
		result.hit = out;
	}
	return result;
}

void Monitor::update(const VulkanRenderer::RenderFrame& frame) {
	ZoneScoped;
	const auto now = std::chrono::steady_clock::now();
	const VoxelStorageDebugInfo* info = frame.voxel_storage ? &frame.voxel_storage->debugInfo() : nullptr;
	const uint64_t sequence = info != nullptr ? info->sequence : 0;
	if (sequence != m_sequence) {
		m_sequence = sequence;
		m_frames_since_upload = 0;
		if (sequence != 0 && !info->patched) {
			m_uploads.push_back(now);
		}
	} else {
		++m_frames_since_upload;
	}
	while (!m_uploads.empty() && now - m_uploads.front() > k_upload_window) {
		m_uploads.pop_front();
	}

	m_probe.reset();
	if (isView(frame.render_mode)) {
		m_probe = probe(frame, frame.imgui_input.mouse_pos);
	}
}

void Monitor::drawOverlay(const VulkanRenderer::RenderFrame& frame) const {
	if (!isView(frame.render_mode)) {
		return;
	}
	const auto view = static_cast<View>(frame.render_mode);

	if (m_probe.has_value()) {
		ImDrawList* list = ImGui::GetForegroundDrawList();
		if (m_probe->hit.has_value()) {
			const Probe& probe = *m_probe->hit;
			const glm::mat4 to_world = voxelDebugToWorld(probe.proxy);
			const glm::vec3 voxel(probe.hit.voxel);
			const glm::vec3 brick = glm::floor(voxel / static_cast<float>(voxel::k_brick_dim)) * static_cast<float>(voxel::k_brick_dim);
			const auto coarse_size = static_cast<float>(voxel::gpu::k_coarse_bricks * voxel::k_brick_dim);
			const glm::vec3 coarse = glm::floor(voxel / coarse_size) * coarse_size;
			const glm::vec3 volume_max = glm::vec3(probe.proxy.brick_dims) * static_cast<float>(voxel::k_brick_dim);

			voxelDebugOutline(list, frame, to_world, glm::vec3(0.0f), volume_max, IM_COL32(255, 255, 255, 60), 1.0f);
			voxelDebugOutline(
			    list, frame, to_world, coarse, glm::min(coarse + coarse_size, volume_max), IM_COL32(255, 255, 255, 150), 1.0f
			);
			voxelDebugOutline(
			    list, frame, to_world, brick, brick + static_cast<float>(voxel::k_brick_dim), IM_COL32(60, 220, 255, 255), 1.5f
			);
			voxelDebugOutline(list, frame, to_world, voxel, voxel + 1.0f, IM_COL32(255, 230, 40, 255), 2.0f);
		} else if (m_probe->miss.has_value()) {
			const Proxy& proxy = m_probe->miss->proxy;
			const glm::vec3 volume_max = glm::vec3(proxy.brick_dims) * static_cast<float>(voxel::k_brick_dim);
			voxelDebugOutline(list, frame, voxelDebugToWorld(proxy), glm::vec3(0.0f), volume_max, IM_COL32(255, 80, 255, 200), 1.5f);
		}
	}

	const ImGuiIO& io = ImGui::GetIO();
	ImGui::SetNextWindowPos(ImVec2(12.0f, io.DisplaySize.y - 12.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
	ImGui::SetNextWindowBgAlpha(0.85f);
	constexpr ImGuiWindowFlags k_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
	                                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
	                                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
	if (ImGui::Begin("##voxel_debug_overlay", nullptr, k_flags)) {
		ImGui::Text("Voxels: %s", voxelDebugViewName(view));
		voxelDebugLegend(view);

		ImGui::Separator();
		voxelDebugProbeText(frame, m_probe);

		ImGui::Separator();
		const auto visible = std::ranges::count_if(frame.voxel_instances, [](const Proxy& proxy) { return proxy.visible; });
		const auto inside =
		    std::ranges::count_if(frame.voxel_instances, [](const Proxy& proxy) { return proxy.visible && proxy.camera_inside; });
		ImGui::TextDisabled(
		    "%zu of %zu volumes drawn, %zu with the camera inside",
		    static_cast<size_t>(std::min<ptrdiff_t>(visible, VoxelPass::k_max_instances)),
		    frame.voxel_instances.size(),
		    static_cast<size_t>(inside)
		);
		if (m_uploads.size() >= k_upload_churn) {
			ImGui::TextColored(k_warn_color, "%zu full uploads in the last 5 s: the scene is re-uploading", m_uploads.size());
		} else {
			ImGui::TextDisabled(
			    "Upload #%llu, %zu full in the last 5 s", static_cast<unsigned long long>(m_sequence), m_uploads.size()
			);
		}
	}
	ImGui::End();
}

void Monitor::drawPanel(const VulkanRenderer::RenderFrame& frame) const {
	if (!ImGui::CollapsingHeader("Voxels")) {
		return;
	}

	const auto& proxies = frame.voxel_instances;
	const auto visible = static_cast<size_t>(std::ranges::count_if(proxies, [](const Proxy& proxy) { return proxy.visible; }));
	const auto inside = static_cast<size_t>(std::ranges::count_if(proxies, [](const Proxy& proxy) {
		return proxy.visible && proxy.camera_inside;
	}));
	const auto mirrored = static_cast<size_t>(std::ranges::count_if(proxies, [](const Proxy& proxy) { return proxy.mirrored; }));
	const size_t drawn = std::min<size_t>(visible, VoxelPass::k_max_instances);

	ImGui::Text("Volumes  %zu, %zu visible, %zu drawn", proxies.size(), visible, drawn);
	if (visible > drawn) {
		ImGui::TextColored(k_warn_color, "%zu visible volumes over the %u draw cap", visible - drawn, VoxelPass::k_max_instances);
	}
	ImGui::TextDisabled("%zu camera inside, %zu mirrored", inside, mirrored);

	if (!frame.voxel_storage) {
		ImGui::TextDisabled("No voxel storage uploaded");
		return;
	}
	const VoxelGpuStorage& storage = *frame.voxel_storage;
	const VoxelStorageDebugInfo& info = storage.debugInfo();

	if (info.patched) {
		ImGui::Text(
		    "Upload   #%llu, patched %u bricks in %.2f ms on the game thread",
		    static_cast<unsigned long long>(info.sequence),
		    info.patched_bricks,
		    info.pack_ms
		);
	} else {
		ImGui::Text(
		    "Upload   #%llu, packed in %.2f ms on the game thread", static_cast<unsigned long long>(info.sequence), info.pack_ms
		);
	}
	if (m_uploads.size() >= k_upload_churn) {
		ImGui::TextColored(k_warn_color, "%zu full uploads in the last 5 s: the scene is re-uploading", m_uploads.size());
	} else {
		ImGui::TextDisabled("%zu full in the last 5 s, %u rendered frames ago", m_uploads.size(), m_frames_since_upload);
	}
	ImGui::TextDisabled("%u pool slots, %u palettes, %u records", info.packed_slots, info.packed_palettes, storage.recordCount());
	ImGui::TextDisabled("CPU mirror %s", info.mirror ? "kept for the cursor probe" : "off, kept only while a voxel view is active");

	constexpr std::array<std::pair<VoxelGpuStorage::Section, const char*>, VoxelGpuStorage::k_section_count> k_sections {
	  {
     {VoxelGpuStorage::Section::materials, "Materials"},
     {VoxelGpuStorage::Section::occupancy, "Occupancy"},
     {VoxelGpuStorage::Section::grids, "Grids"},
     {VoxelGpuStorage::Section::coarse, "Coarse"},
     {VoxelGpuStorage::Section::palettes, "Palettes"},
     {VoxelGpuStorage::Section::records, "Records"},
	   }
	};
	constexpr ImGuiTableFlags k_table_flags =
	    ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
	if (ImGui::BeginTable("##voxel_sections", 2, k_table_flags)) {
		uint64_t total = 0;
		for (const auto& [section, label] : k_sections) {
			const uint64_t bytes = storage.size(section);
			total += bytes;
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(label);
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(voxelDebugBytes(bytes).c_str());
		}
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextUnformatted("GPU total");
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(voxelDebugBytes(total).c_str());
		ImGui::EndTable();
	}

	VoxelStorageDebugInfo::BrickCensus totals;
	for (const auto& census : info.bricks) {
		totals.uniform += census.uniform;
		totals.shared += census.shared;
		totals.owned += census.owned;
	}
	voxelDebugSwatch(k_uniform_brick_color, std::format("{} uniform bricks", totals.uniform).c_str());
	voxelDebugSwatch(k_shared_brick_color, std::format("{} shared bricks", totals.shared).c_str());
	voxelDebugSwatch(k_owned_brick_color, std::format("{} owned bricks", totals.owned).c_str());

	if (proxies.empty()) {
		return;
	}

	constexpr ImGuiTableFlags k_volume_flags =
	    ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY;
	const float height = ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(std::min<size_t>(proxies.size(), 12) + 1);
	if (ImGui::BeginTable("##voxel_volumes", 8, k_volume_flags, ImVec2(0.0f, height))) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Node");
		ImGui::TableSetupColumn("Rec");
		ImGui::TableSetupColumn("Bricks");
		ImGui::TableSetupColumn("Uniform");
		ImGui::TableSetupColumn("Shared");
		ImGui::TableSetupColumn("Owned");
		ImGui::TableSetupColumn("State");
		ImGui::TableSetupColumn("Dist");
		ImGui::TableHeadersRow();

		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(proxies.size()));
		while (clipper.Step()) {
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
				const Proxy& proxy = proxies[static_cast<size_t>(row)];
				const std::string_view name = voxelDebugNodeName(frame, proxy.record_index);
				const VoxelStorageDebugInfo::BrickCensus census =
				    proxy.record_index < info.bricks.size() ? info.bricks[proxy.record_index] : VoxelStorageDebugInfo::BrickCensus {};

				std::string state = proxy.visible ? "" : "culled";
				if (proxy.camera_inside) {
					state += state.empty() ? "inside" : " inside";
				}
				if (proxy.mirrored) {
					state += state.empty() ? "mirrored" : " mirrored";
				}

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%.*s", static_cast<int>(name.size()), name.data());
				ImGui::TableNextColumn();
				ImGui::Text("%u", proxy.record_index);
				ImGui::TableNextColumn();
				ImGui::Text("%ux%ux%u", proxy.brick_dims.x, proxy.brick_dims.y, proxy.brick_dims.z);
				ImGui::TableNextColumn();
				ImGui::Text("%u", census.uniform);
				ImGui::TableNextColumn();
				ImGui::Text("%u", census.shared);
				ImGui::TableNextColumn();
				ImGui::Text("%u", census.owned);
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(state.c_str());
				ImGui::TableNextColumn();
				ImGui::Text("%.1f m", proxy.view_distance);
			}
		}
		ImGui::EndTable();
	}
}

}
