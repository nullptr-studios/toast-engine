#include "voxel_thumbnail.h"

#include "voxel_model.hpp"
#include "voxel_palette.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <toast/log.hpp>
#include <toast/voxel/voxel_constants.hpp>
#include <tracy/Tracy.hpp>
#include <vector>

namespace {

using voxel::k_brick_dim;
using voxel::k_empty_palette_index;
using voxel::Palette;

constexpr uint32_t k_supersample = 4;

struct RgbaF {
	float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
};

[[nodiscard]]
auto voxThumbReadFile(const std::filesystem::path& path) -> std::vector<uint8_t> {
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		return {};
	}
	return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

struct VoxThumbGrid {
	glm::uvec3 dims {0};    // in voxels
	std::vector<uint8_t> materials;

	[[nodiscard]]
	auto at(int32_t x, int32_t y, int32_t z) const noexcept -> uint8_t {
		if (x < 0 || y < 0 || z < 0 || static_cast<uint32_t>(x) >= dims.x || static_cast<uint32_t>(y) >= dims.y ||
		    static_cast<uint32_t>(z) >= dims.z) {
			return k_empty_palette_index;
		}
		return materials[static_cast<size_t>(x) + (static_cast<size_t>(y) * dims.x) + (static_cast<size_t>(z) * dims.x * dims.y)];
	}
};

[[nodiscard]]
auto voxThumbBuildGrid(const assets::VoxelModel& model) -> VoxThumbGrid {
	VoxThumbGrid grid;
	grid.dims = model.brickDims() * k_brick_dim;
	grid.materials.resize(static_cast<size_t>(grid.dims.x) * grid.dims.y * grid.dims.z);

	size_t i = 0;
	for (uint32_t z = 0; z < grid.dims.z; ++z) {
		for (uint32_t y = 0; y < grid.dims.y; ++y) {
			for (uint32_t x = 0; x < grid.dims.x; ++x, ++i) {
				grid.materials[i] = model.materialAt(glm::uvec3(x, y, z));
			}
		}
	}
	return grid;
}

[[nodiscard]]
auto voxThumbCenterOfMass(const VoxThumbGrid& grid) -> std::optional<glm::vec3> {
	glm::dvec3 sum(0.0);
	uint64_t count = 0;
	for (uint32_t z = 0; z < grid.dims.z; ++z) {
		for (uint32_t y = 0; y < grid.dims.y; ++y) {
			for (uint32_t x = 0; x < grid.dims.x; ++x) {
				if (grid.at(static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z)) != k_empty_palette_index) {
					sum += glm::dvec3(x + 0.5, y + 0.5, z + 0.5);
					++count;
				}
			}
		}
	}
	if (count == 0) {
		return std::nullopt;
	}
	return glm::vec3(sum / static_cast<double>(count));
}

[[nodiscard]]
auto voxThumbAlbedo(const Palette& palette, uint8_t index) -> glm::vec3 {
	const voxel::PaletteEntry& entry = palette.entries[index];
	return glm::vec3(entry.albedo_r, entry.albedo_g, entry.albedo_b) / 255.0f;
}

[[nodiscard]]
auto voxThumbShade(glm::vec3 albedo, glm::vec3 normal) -> glm::vec3 {
	constexpr glm::vec3 k_light_dir = glm::vec3(-0.4f, -0.55f, 0.73f);
	constexpr float k_ambient = 0.45f;
	constexpr float k_diffuse = 0.55f;
	const float n_dot_l = std::max(0.0f, glm::dot(normal, k_light_dir));
	return albedo * (k_ambient + (k_diffuse * n_dot_l));
}

struct VoxThumbCamera {
	glm::vec3 origin_center {0.0f};
	glm::vec3 right {1.0f, 0.0f, 0.0f};
	glm::vec3 up {0.0f, 0.0f, 1.0f};
	glm::vec3 forward {0.0f, 1.0f, 0.0f};
	float half_extent = 1.0f;
};

[[nodiscard]]
auto voxThumbBuildCamera(const VoxThumbGrid& grid, glm::vec3 center_of_mass) -> VoxThumbCamera {
	VoxThumbCamera cam;

	constexpr float k_yaw = glm::radians(45.0f);
	constexpr float k_pitch = glm::radians(30.0f);
	const glm::vec3 forward =
	    glm::normalize(glm::vec3(std::cos(k_yaw) * std::cos(k_pitch), std::sin(k_yaw) * std::cos(k_pitch), -std::sin(k_pitch)));

	cam.forward = forward;
	cam.right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 0.0f, 1.0f)));
	cam.up = glm::normalize(glm::cross(cam.right, forward));
	cam.origin_center = center_of_mass;

	float max_extent = 0.001f;
	for (int corner = 0; corner < 8; ++corner) {
		const glm::vec3 p(
		    (corner & 1) != 0 ? static_cast<float>(grid.dims.x) : 0.0f,
		    (corner & 2) != 0 ? static_cast<float>(grid.dims.y) : 0.0f,
		    (corner & 4) != 0 ? static_cast<float>(grid.dims.z) : 0.0f
		);
		const glm::vec3 rel = p - center_of_mass;
		max_extent = std::max(max_extent, std::abs(glm::dot(rel, cam.right)));
		max_extent = std::max(max_extent, std::abs(glm::dot(rel, cam.up)));
	}
	cam.half_extent = max_extent * 1.05f;
	return cam;
}

struct VoxThumbHit {
	uint8_t material = k_empty_palette_index;
	glm::vec3 normal {0.0f};
};

[[nodiscard]]
auto voxThumbRaycast(const VoxThumbGrid& grid, glm::vec3 origin, glm::vec3 dir) -> VoxThumbHit {
	const glm::vec3 bounds_min(0.0f);
	const glm::vec3 bounds_max(grid.dims);

	float t_enter = 0.0f;
	float t_exit = std::numeric_limits<float>::max();
	for (int axis = 0; axis < 3; ++axis) {
		const float d = dir[axis];
		const float o = origin[axis];
		if (std::abs(d) < 1e-8f) {
			if (o < bounds_min[axis] || o > bounds_max[axis]) {
				return {};
			}
			continue;
		}
		float t0 = (bounds_min[axis] - o) / d;
		float t1 = (bounds_max[axis] - o) / d;
		if (t0 > t1) {
			std::swap(t0, t1);
		}
		t_enter = std::max(t_enter, t0);
		t_exit = std::min(t_exit, t1);
	}
	if (t_enter > t_exit) {
		return {};
	}

	glm::vec3 pos = origin + (dir * (t_enter + 1e-4f));
	glm::ivec3 cell(std::floor(pos.x), std::floor(pos.y), std::floor(pos.z));
	cell = glm::clamp(cell, glm::ivec3(0), glm::ivec3(grid.dims) - 1);

	glm::ivec3 step;
	glm::vec3 t_delta;
	glm::vec3 t_max;
	for (int axis = 0; axis < 3; ++axis) {
		if (dir[axis] > 0.0f) {
			step[axis] = 1;
			t_delta[axis] = 1.0f / dir[axis];
			t_max[axis] = ((static_cast<float>(cell[axis]) + 1.0f) - origin[axis]) / dir[axis];
		} else if (dir[axis] < 0.0f) {
			step[axis] = -1;
			t_delta[axis] = -1.0f / dir[axis];
			t_max[axis] = (static_cast<float>(cell[axis]) - origin[axis]) / dir[axis];
		} else {
			step[axis] = 0;
			t_delta[axis] = std::numeric_limits<float>::max();
			t_max[axis] = std::numeric_limits<float>::max();
		}
	}

	glm::vec3 last_normal(0.0f);
	const int max_steps = static_cast<int>(grid.dims.x + grid.dims.y + grid.dims.z) + 4;
	for (int i = 0; i < max_steps; ++i) {
		const uint8_t mat = grid.at(cell.x, cell.y, cell.z);
		if (mat != k_empty_palette_index) {
			return {mat, last_normal};
		}

		if (t_max.x < t_max.y && t_max.x < t_max.z) {
			cell.x += step.x;
			t_max.x += t_delta.x;
			last_normal = glm::vec3(static_cast<float>(-step.x), 0.0f, 0.0f);
		} else if (t_max.y < t_max.z) {
			cell.y += step.y;
			t_max.y += t_delta.y;
			last_normal = glm::vec3(0.0f, static_cast<float>(-step.y), 0.0f);
		} else {
			cell.z += step.z;
			t_max.z += t_delta.z;
			last_normal = glm::vec3(0.0f, 0.0f, static_cast<float>(-step.z));
		}

		if (cell.x < 0 || cell.y < 0 || cell.z < 0 || static_cast<uint32_t>(cell.x) >= grid.dims.x ||
		    static_cast<uint32_t>(cell.y) >= grid.dims.y || static_cast<uint32_t>(cell.z) >= grid.dims.z) {
			break;
		}
	}
	return {};
}

}

extern "C" {

auto toast_tvox_render_thumbnail(const char* tvox_path, const char* palette_path, uint8_t* dst, uint32_t thumb_size) noexcept
    -> int {
	ZoneScoped;
	try {
		const std::vector<uint8_t> bytes = voxThumbReadFile(tvox_path);
		if (bytes.empty()) {
			TOAST_ERROR("AssetManager", "toast_tvox_render_thumbnail: could not open '{}'", tvox_path);
			return 0;
		}
		const assets::VoxelModel model(bytes);
		const VoxThumbGrid grid = voxThumbBuildGrid(model);

		Palette palette;
		for (uint32_t i = 1; i < voxel::k_palette_size; ++i) {
			palette.entries[i].albedo_r = 160;
			palette.entries[i].albedo_g = 160;
			palette.entries[i].albedo_b = 160;
		}
		if (palette_path != nullptr && *palette_path != '\0') {
			const std::vector<uint8_t> palette_bytes = voxThumbReadFile(palette_path);
			if (!palette_bytes.empty()) {
				try {
					const std::string toml_text(palette_bytes.begin(), palette_bytes.end());
					const std::unique_ptr<assets::VoxelPalette> loaded = assets::VoxelPalette::fromToml(toml::parse(toml_text));
					palette = loaded->palette();
				} catch (const std::exception& e) {
					TOAST_WARN("AssetManager", "toast_tvox_render_thumbnail: could not parse palette '{}': {}", palette_path, e.what());
				}
			}
		}

		std::fill_n(dst, static_cast<size_t>(thumb_size) * thumb_size * 4, uint8_t {0});

		const std::optional<glm::vec3> center_of_mass = voxThumbCenterOfMass(grid);
		if (!center_of_mass.has_value()) {
			return 1;
		}
		const VoxThumbCamera cam = voxThumbBuildCamera(grid, *center_of_mass);

		const uint32_t super_size = thumb_size * k_supersample;
		std::vector<RgbaF> samples(static_cast<size_t>(super_size) * super_size);

		for (uint32_t sy = 0; sy < super_size; ++sy) {
			const float v = (((static_cast<float>(sy) + 0.5f) / static_cast<float>(super_size)) * -2.0f + 1.0f) * cam.half_extent;
			for (uint32_t sx = 0; sx < super_size; ++sx) {
				const float u = (((static_cast<float>(sx) + 0.5f) / static_cast<float>(super_size)) * 2.0f - 1.0f) * cam.half_extent;

				const glm::vec3 plane_point = cam.origin_center + (cam.right * u) + (cam.up * v);
				const glm::vec3 origin = plane_point - (cam.forward * 10000.0f);

				const VoxThumbHit hit = voxThumbRaycast(grid, origin, cam.forward);
				RgbaF& out = samples[(static_cast<size_t>(sy) * super_size) + sx];
				if (hit.material != k_empty_palette_index) {
					const glm::vec3 shaded = voxThumbShade(voxThumbAlbedo(palette, hit.material), hit.normal);
					out = {shaded.r, shaded.g, shaded.b, 1.0f};
				}
			}
		}

		for (uint32_t y = 0; y < thumb_size; ++y) {
			for (uint32_t x = 0; x < thumb_size; ++x) {
				float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
				for (uint32_t oy = 0; oy < k_supersample; ++oy) {
					for (uint32_t ox = 0; ox < k_supersample; ++ox) {
						const RgbaF& s = samples[(static_cast<size_t>((y * k_supersample) + oy) * super_size) + (x * k_supersample) + ox];
						r += s.r * s.a;
						g += s.g * s.a;
						b += s.b * s.a;
						a += s.a;
					}
				}
				constexpr float k_samples_per_texel = static_cast<float>(k_supersample * k_supersample);
				a /= k_samples_per_texel;
				if (a > 1e-6f) {
					const float inv_coverage_sum = 1.0f / (a * k_samples_per_texel);
					r *= inv_coverage_sum;
					g *= inv_coverage_sum;
					b *= inv_coverage_sum;
				}

				uint8_t* px = &dst[((static_cast<size_t>(y) * thumb_size) + x) * 4];
				px[0] = static_cast<uint8_t>(std::clamp(r * 255.0f, 0.0f, 255.0f));
				px[1] = static_cast<uint8_t>(std::clamp(g * 255.0f, 0.0f, 255.0f));
				px[2] = static_cast<uint8_t>(std::clamp(b * 255.0f, 0.0f, 255.0f));
				px[3] = static_cast<uint8_t>(std::clamp(a * 255.0f, 0.0f, 255.0f));
			}
		}

		return 1;
	} catch (const std::exception& e) {
		TOAST_ERROR("AssetManager", "toast_tvox_render_thumbnail failed: {}", e.what());
		return 0;
	} catch (...) {
		TOAST_ERROR("AssetManager", "toast_tvox_render_thumbnail failed with an unrecognized exception");
		return 0;
	}
}
}
