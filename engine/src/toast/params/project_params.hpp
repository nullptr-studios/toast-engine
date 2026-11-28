/**
 * @file project_params.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief The project level settings that used to live in
 */

#pragma once
#include <string>
#include <toast/params/group.hpp>
#include <vector>

namespace toast::params {

struct ProjectParams : Group<"project"> {
	std::vector<std::string> databases = param(
	    &databases, "databases", {"assets"},
	    {.name = "Databases", .description = "Content folders; each one becomes a name:// root", .requires_restart = true}
	);
	uint32_t max_tick_rate = param(
	    &max_tick_rate, "max_tick_rate", 500,
	    {.name = "Max tick rate", .unit = "Hz", .description = "Ticks per second in the editor and the player. 0 is uncapped"}
	);
};

struct GameplayParams : Group<"gameplay"> {
	AssetRef init_scene = param(
	    &init_scene, "init_scene", AssetRef {},
	    {.name = "Start scene", .description = "Loaded when the game starts", .asset_type = "node"}
	);
	AssetRef player = param(
	    &player, "player", AssetRef {},
	    {.name = "Player", .description = "Prefab spawned when the game starts", .asset_type = "node"}
	);
};

struct UIParams : Group<"ui"> {
	AssetRef color_scheme = param(
	    &color_scheme, "color_scheme", AssetRef {},
	    {.name = "Color scheme", .description = "Fallback for color formatting in the UI", .asset_type = "color_scheme"}
	);
	float scale = param(
	    &scale, "scale", 1.0F,
	    {.name = "Interface scale", .min = 0.5F, .max = 3.0F, .description = "Makes the whole interface bigger or smaller"}
	);
	float scroll_speed = param(
	    &scroll_speed, "scroll_speed", 1.0F,
	    {.name = "Scroll speed", .min = 0.1F, .max = 10.0F, .description = "How far the wheel scrolls lists"}
	);
	std::vector<std::string> languages =
	    param(&languages, "languages", {"en"}, {.name = "Languages", .description = "The first one is the default"});
	std::string language = param(
	    &language, "language", std::string {},
	    {.name = "Language",
			 .kind = Kind::debug,
			 .description =
			     "The language the UI is showing right now. Set it from the console to preview another one; empty goes back to "
			     "the default"}
	);
};

}
