/**
 * @file brush_composer.hpp
 * @author Xein
 * @date 29 Oct 2026
 * @brief Turns a VoxelBrush prefab into a voxel model, in memory
 */

#pragma once
#include <memory>
#include <toast/export.hpp>
#include <toast/uid.hpp>

namespace assets {
class Prefab;
class VoxelModel;
}

namespace toast {
class ProceduralVoxel;
}

namespace voxel {

[[nodiscard]]
TOAST_API auto isBrushPrefab(const assets::Prefab& prefab) -> bool;

[[nodiscard]]
TOAST_API auto composeBrushTree(toast::ProceduralVoxel& brush) -> std::unique_ptr<assets::VoxelModel>;

[[nodiscard]]
TOAST_API auto brushModel(const assets::Prefab& prefab, toast::UID uid) -> const assets::VoxelModel*;

TOAST_API void invalidateBrushes() noexcept;

}
