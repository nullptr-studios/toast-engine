/**
 * @file asset_params.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief When assets reload
 */

#pragma once
#include <cstdint>
#include <toast/export.hpp>
#include <toast/params/group.hpp>

namespace assets {

struct AssetParams : toast::params::Group<"assets"> {
	using Kind = toast::params::Kind;

	bool hot_reload_enabled = param(
	    &hot_reload_enabled, "hot_reload.enabled", true, {.name = "Hot reload", .description = "Pick up files that changed on disk"}
	);
	float hot_reload_interval = param(
	    &hot_reload_interval, "hot_reload.interval", 1.0F,
	    {.name = "Check every", .min = 0.1F, .max = 60.0F, .unit = "s", .description = "How often the disk is checked"}
	);
	float unload_interval = param(
	    &unload_interval, "unload.interval", 30.0F,
	    {.name = "Free unused every",
			 .min = 0.0F,
			 .max = 3600.0F,
			 .unit = "s",
			 .description = "Assets nothing uses are released this often. 0 keeps them"}
	);
	float skinned_bounds_padding = param(
	    &skinned_bounds_padding, "import.skinned_bounds_padding", 1.5F,
	    {.name = "Skinned bounds padding",
			 .min = 1.0F,
			 .max = 4.0F,
			 .description = "Culling bounds of skinned meshes grow by this much so animation does not pop out of view"}
	);
	int32_t material_parent_depth = param(
	    &material_parent_depth, "materials.max_parent_depth", 16,
	    {.name = "Parent chain limit",
			 .min = 1,
			 .max = 64,
			 .kind = Kind::advanced,
			 .description = "Longest chain of parent materials before it counts as a loop"}
	);
};

TOAST_API auto assetParams() -> AssetParams&;

}
