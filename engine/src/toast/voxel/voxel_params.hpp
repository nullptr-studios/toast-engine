/**
 * @file voxel_params.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief How much memory voxels may take and how big
 */

#pragma once
#include <cstdint>
#include <toast/export.hpp>
#include <toast/params/group.hpp>

namespace voxel {

struct VoxelParams : toast::params::Group<"voxel"> {
	using Kind = toast::params::Kind;

	int32_t runtime_bricks = param(
	    &runtime_bricks, "pools.runtime_bricks", 1 << 20,
	    {.name = "Runtime bricks",
			 .min = 1 << 12,
			 .max = 1 << 24,
			 .unit = "bricks",
			 .description = "Room for the voxels of everything in the scene",
			 .requires_restart = true}
	);
	int32_t procedural_bricks = param(
	    &procedural_bricks, "pools.procedural_bricks", 1 << 18,
	    {.name = "Procedural bricks",
			 .min = 1 << 10,
			 .max = 1 << 24,
			 .unit = "bricks",
			 .description = "Room for generated voxels",
			 .requires_restart = true}
	);
	int32_t max_bricks_per_axis = param(
	    &max_bricks_per_axis, "limits.max_bricks_per_axis", 512,
	    {.name = "Longest side", .min = 8, .max = 4096, .unit = "bricks", .description = "Larger volumes are refused"}
	);
	int64_t max_bricks = param(
	    &max_bricks, "limits.max_bricks", int64_t {1} << 21,
	    {.name = "Volume size",
			 .min = int64_t {1} << 10,
			 .max = int64_t {1} << 28,
			 .unit = "bricks",
			 .description = "Volumes with more bricks than this are refused"}
	);
	int32_t thumbnail_supersample = param(
	    &thumbnail_supersample, "thumbnails.supersample", 4,
	    {.name = "Thumbnail samples", .min = 1, .max = 8, .description = "Samples per pixel side. More is smoother and slower"}
	);
};

TOAST_API auto voxelParams() -> VoxelParams&;

}
