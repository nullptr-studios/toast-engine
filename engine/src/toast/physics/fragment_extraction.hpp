/**
 * @file fragment_extraction.hpp
 * @author Xein
 * @date 20 Sep 2026
 */

#pragma once

#include "component_classification.hpp"

#include <array>
#include <glm/glm.hpp>
#include <optional>
#include <toast/voxel/mass_accumulator.hpp>
#include <toast/voxel/palette.hpp>
#include <toast/voxel/voxel_volume.hpp>

namespace physics {

using DensityTable = std::array<uint32_t, voxel::k_palette_size>;

struct RemovedVoxels {
	/// In source voxel coordinates
	voxel::MassMoments moments;
	uint32_t voxels = 0;
};

struct ExtractedFragment {
	voxel::Volume volume;
	glm::ivec3 offset;

	/// In fragment voxel coordinates
	voxel::MassMoments moments;
	uint32_t voxels = 0;
};

/// @returns nullopt with source untouched when the pool is short of bricks
[[nodiscard]]
auto extractFragmentVolume(voxel::Volume& source, const DetachedComponent& component, const DensityTable& density)
    -> std::optional<ExtractedFragment>;

void restoreFragment(voxel::Volume& source, voxel::Volume& fragment, glm::ivec3 offset);

auto discardComponent(voxel::Volume& source, const DetachedComponent& component, const DensityTable& density) -> RemovedVoxels;

}
