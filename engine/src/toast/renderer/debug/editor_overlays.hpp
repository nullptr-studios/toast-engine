/**
 * @file editor_overlays.hpp
 * @author Xein
 * @date 26 Sep 2026
 * @brief What the VoxelEditor asks the viewport to draw on top of the scene
 */

#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <toast/export.hpp>
#include <toast/uid.hpp>

namespace toast {
class VoxelPiece;
}

namespace renderer {

struct EditorOverlays {
	bool surface_unit_grid = false;     ///< 1 m lines drawn on the voxel surface
	bool surface_voxel_grid = false;    ///< 0.1m lines drawn on the voxel surface
	bool voxel_edges = false;           ///< Lines along the real edges of the voxels
	bool volume_edges = false;          ///< Lines along the AABB volume for the voxels
	bool projections = false;           ///< 2D diedrico projections peak dibujo tecnico

	toast::UID selected;                ///< Focused node

	toast::UID cut_root;                ///< The voxel the Split and Slice preview is on
	bool cut_active = false;
	glm::vec4 cut_plane {0.0f};         ///< red is where the normal points

	bool tool_box_active = false;
	toast::UID tool_root;
	glm::ivec3 tool_box_min {0};
	glm::ivec3 tool_box_max {0};
	glm::vec4 tool_box_color {1.0f};
	uint8_t tool_box_kind = 0;                  ///< 0 fill, 1 carve, 2 paint, 3 outline only
	toast::VoxelPiece* tool_piece = nullptr;    ///< Draws the preview with the script new volumes get

	/// A second outline, Extrude shows both parts of the face it is splitting
	bool tool_box2_active = false;
	glm::ivec3 tool_box2_min {0};
	glm::ivec3 tool_box2_max {0};
};

/// @note Main thread only, the editor events set it and the passes read it while recording
[[nodiscard]]
TOAST_API auto editorOverlays() -> EditorOverlays&;

}
