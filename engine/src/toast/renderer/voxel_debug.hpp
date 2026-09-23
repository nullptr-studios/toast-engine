/**
 * @file voxel_debug.hpp
 * @author dario
 * @date 19/09/2026
 */

#pragma once
#include "vulkan_renderer.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <string_view>
#include <toast/voxel/palette.hpp>
#include <toast/voxel/ray_march.hpp>

namespace renderer::voxel_debug {

/// Mirrors lighting.slang kRenderModeVoxel and the editor RenderMode enum
enum class View : uint8_t {
	steps = 20,
	traversal = 21,
	bricks = 22,
	volumes = 23,
	materials = 24,
};

[[nodiscard]]
constexpr auto isView(uint32_t render_mode) noexcept -> bool {
	return render_mode >= static_cast<uint32_t>(View::steps) && render_mode <= static_cast<uint32_t>(View::materials);
}

struct Rgb {
	float r = 0.0f;
	float g = 0.0f;
	float b = 0.0f;
};

/// Mirrors voxel.slang kStepBandColors
inline constexpr std::array<Rgb, 8> k_step_band_colors {
  Rgb {0.10f, 0.15f, 0.55f},
  Rgb {0.10f, 0.45f, 0.90f},
  Rgb {0.10f, 0.75f, 0.75f},
  Rgb {0.25f, 0.80f, 0.25f},
  Rgb {0.90f, 0.85f, 0.15f},
  Rgb {0.95f, 0.50f, 0.10f},
  Rgb {0.90f, 0.15f, 0.10f},
  Rgb {0.85f, 0.20f, 0.85f},
};
inline constexpr std::array<uint32_t, 8> k_step_band_starts {1, 4, 8, 16, 32, 64, 128, 256};
inline constexpr Rgb k_exhausted_color {1.0f, 1.0f, 1.0f};
inline constexpr Rgb k_uniform_brick_color {0.20f, 0.40f, 0.95f};
inline constexpr Rgb k_shared_brick_color {0.25f, 0.75f, 0.30f};
inline constexpr Rgb k_owned_brick_color {0.95f, 0.55f, 0.12f};
inline constexpr Rgb k_default_material_color {0.90f, 0.10f, 0.90f};

/// From voxel::marchRay over the uploaded data
struct Probe {
	VulkanRenderer::VoxelVolumeProxy proxy;
	voxel::RayHit hit;

	glm::vec3 world_position {0.0f};
	float distance = 0.0f;

	/// Volume space and zero when the ray started inside a solid voxel
	glm::ivec3 face {0};

	uint32_t brick_entry = 0;
	voxel::PaletteEntry palette_entry {};
	float max_emissive = 1.0f;
};

/// The costliest box the cursor ray entered without hitting anything
struct MissProbe {
	VulkanRenderer::VoxelVolumeProxy proxy;
	voxel::RayHit hit;
};

struct ProbeResult {
	std::optional<Probe> hit;
	std::optional<MissProbe> miss;
};

/// @returns nothing without the CPU mirror
/// @note Voxels only so a mesh in front of the cursor is ignored
[[nodiscard]]
auto probe(const VulkanRenderer::RenderFrame& frame, glm::vec2 cursor) -> std::optional<ProbeResult>;

/// Render thread only
class Monitor {
public:
	void update(const VulkanRenderer::RenderFrame& frame);

	/// Draws nothing outside a voxel view
	void drawOverlay(const VulkanRenderer::RenderFrame& frame) const;

	/// Call inside the Toast Debug window
	void drawPanel(const VulkanRenderer::RenderFrame& frame) const;

private:
	uint64_t m_sequence = 0;
	uint32_t m_frames_since_upload = 0;
	std::deque<std::chrono::steady_clock::time_point> m_uploads;

	std::optional<ProbeResult> m_probe;
};

}
