/**
 * @file vox_import.hpp
 * @author dario
 * @date 12/09/2026
 */

#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <string>
#include <toast/export.hpp>
#include <toast/voxel/brick_pool.hpp>
#include <toast/voxel/palette.hpp>
#include <toast/voxel/stamp.hpp>
#include <toast/voxel/voxel_volume.hpp>
#include <vector>

namespace assets {

inline constexpr uint32_t k_vox_max_dim = 256;

struct VoxVoxel {
	uint8_t x = 0;
	uint8_t y = 0;
	uint8_t z = 0;

	/// 1-255 since a .vox never stores an empty voxel
	uint8_t palette_index = 0;
};

struct VoxModel {
	glm::uvec3 dims {0};
	std::vector<VoxVoxel> voxels;
};

/// @brief Translation positions the node centre not its corner
struct VoxTransform {
	toast::voxel::LatticeOrientation orientation;
	glm::ivec3 translation {0};

	[[nodiscard]]
	auto operator==(const VoxTransform&) const noexcept -> bool = default;
};

struct VoxNode {
	std::string name;

	VoxTransform local;

	bool hidden = false;

	std::vector<uint32_t> children;

	std::optional<uint32_t> model;
};

struct VoxScene {
	std::vector<VoxModel> models;

	/// nodes[0] is the root
	std::vector<VoxNode> nodes;

	toast::voxel::Palette palette;

	std::vector<std::string> warnings;
};

/// @brief Bits 0-1 source axis of x bits 2-3 source axis of y bits 4-6 negate x y z so the identity is 4 not 0
[[nodiscard]]
TOAST_API auto voxOrientationFromByte(uint8_t rotation) -> std::optional<toast::voxel::LatticeOrientation>;

[[nodiscard]]
TOAST_API auto voxOrientationToByte(const toast::voxel::LatticeOrientation& orientation) -> uint8_t;

[[nodiscard]]
TOAST_API auto composeVoxTransforms(const VoxTransform& parent, const VoxTransform& local) -> VoxTransform;

/// @brief Half the size moves into the offset floored on unflipped axes and rounded up on flipped ones
[[nodiscard]]
TOAST_API auto voxPlacementOf(const VoxTransform& world, glm::uvec3 model_dims) -> toast::voxel::LatticePlacement;

struct VoxPlacement {
	uint32_t model = 0;
	std::string name;
	bool hidden = false;
	toast::voxel::LatticePlacement placement;
};

[[nodiscard]]
TOAST_API auto flattenVoxScene(const VoxScene& scene) -> std::vector<VoxPlacement>;

[[nodiscard]]
TOAST_API auto buildVoxVolume(const VoxModel& model, toast::voxel::BrickPool& pool) -> std::optional<toast::voxel::Volume>;

/// @throws std::runtime_error on anything unrepresentable
[[nodiscard]]
TOAST_API auto importVox(std::span<const uint8_t> data) -> VoxScene;

}
