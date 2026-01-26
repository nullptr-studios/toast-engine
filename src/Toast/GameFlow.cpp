#include "Toast/GameFlow.hpp"

#include "Toast/Log.hpp"
#include "Toast/Resources/ResourceManager.hpp"
#include "Toast/World.hpp"
#include "sol/state.hpp"

#include <future>
#include <optional>

namespace toast {

GameFlow::GameFlow() {
	sol::state lua;
	sol::table lua_table;

	std::vector<std::string> world_list;

	try {
		// Loading the lua file
		lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::table);

		auto file = resource::Open("gameflow.lua");
		if (!file.has_value()) {
			TOAST_ERROR("File couldn't be open");
			throw sol::error("scenes.lua does not exist or cannot be opened");
		}

		sol::optional<sol::table> result = lua.script(*file);
		if (!result.has_value()) {
			TOAST_ERROR("Lua file didn't return anything");
			throw sol::error("scenes.lua did not return a lua table???");
		}
		lua_table = *result;

		world_list = lua_table.as<std::vector<std::string>>();

	} catch (const sol::error& e) {    //
		TOAST_WARN("Scenes.lua file failed to do something: {}", e.what());
	}
	m = {
		.worldList = std::move(world_list),
		.levelList = {},
		.world = std::nullopt,
		.level = std::nullopt,
		.currentLevel = std::nullopt,
		.nextLevel = std::nullopt,
	};
}

void GameFlow::LoadWorld(unsigned world) {
	if (m.world == world || m.worldList.size() <= world) {
		return;
	}

	m.world = world;
	m.level = std::nullopt;

	sol::state lua;
	sol::table lua_table;

	try {
		// Loading the lua file
		lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::table);

		auto file = resource::Open(m.worldList[world]);
		if (!file.has_value()) {
			TOAST_ERROR("File couldn't be open");
			throw sol::error("scenes.lua does not exist or cannot be opened");
		}

		sol::optional<sol::table> result = lua.script(*file);
		if (!result.has_value()) {
			TOAST_ERROR("Lua file didn't return anything");
			throw sol::error("scenes.lua did not return a lua table???");
		}
		lua_table = *result;

		m.levelList = lua_table.as<std::vector<std::string>>();

	} catch (const sol::error& e) { TOAST_WARN("Scenes.lua file failed to do something: {}", e.what()); }

	m.nextLevel = toast::World::LoadScene(m.levelList[0]);
}

void GameFlow::LoadLevel(unsigned world, unsigned level) {
	LoadWorld(world);
	if (m.level == level || m.levelList.size() <= level) {
		return;
	}
	m.level = level;

	m.currentLevel = toast::World::LoadScene(m.levelList[level]);
	if (m.levelList.size() > level + 1) {
		m.nextLevel = toast::World::LoadScene(m.levelList[level + 1]);
	} else {
		m.nextLevel = std::nullopt;
	}
	m.currentLevel->wait();
	auto* scene = toast::World::Get(m.currentLevel->get());
	scene->enabled(true);
}

void GameFlow::NextLevel() {
	m.level = m.level.or_else([] {
		return std::optional<unsigned>(0);
	});

	// Nuke Loaded Level
	if (m.currentLevel.has_value()) {
		auto* scene = toast::World::Get(m.currentLevel->get());
		scene->Nuke();
		m.currentLevel = std::nullopt;
	}

	// Increment Level
	if (m.levelList.size() <= m.level.value() + 1) {
		TOAST_WARN("End Of the World...");
		m.level = std::nullopt;
		return;
	}
	m.level = m.level.value() + 1;

	// Load & Enable New Level
	m.currentLevel = std::move(m.nextLevel).or_else([this] -> std::optional<std::future<unsigned>> {
		return toast::World::LoadScene(m.levelList[m.level.value()]);
	});
	m.currentLevel->wait();
	auto* scene = toast::World::Get(m.currentLevel->get());
	scene->enabled(true);

	// Pre Load Next Level :3
	if (m.levelList.size() <= m.level.value() + 1) {
		m.nextLevel = toast::World::LoadScene(m.levelList[m.level.value() + 1]);
	} else {
		m.nextLevel = std::nullopt;
	}
}

void GameFlow::NextWorld() {
	// Loads Next World Unless Next World unless no worlds are loaded (loads the first world)
	// clang-format off
	LoadWorld(
    m.world.transform([](unsigned val) {
      return val + 1;
    }).value_or(0)
  );
	// clang-format on
}
}
