/**
 * @file FragmentStraction.hpp
 * @author Xein
 * @date 20 Sep 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once

#include "component_classification.hpp"

#include <glm/glm.hpp>
#include <toast/voxel/voxel_volume.hpp>

namespace physics {

struct ExtractedFragment {
	voxel::Volume volume;
	glm::ivec3 offset;
};

[[nodiscard]]
auto extractFragmentVolume(const voxel::Volume& source, const DetachedComponent& component) -> ExtractedFragment;

}
