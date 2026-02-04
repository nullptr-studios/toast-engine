#include "Toast/GameFlow.hpp"

#include "Toast/GameEvents.hpp"
#include "Toast/Log.hpp"
#include "Toast/Resources/ResourceManager.hpp"
#include "Toast/World.hpp"
#include "sol/forward.hpp"
#include "sol/state.hpp"

#include <future>
#include <optional>

namespace toast {

GameFlow::GameFlow() {
	sol::state lua;

	std::vector<std::string> world_list;

	// Loading the lua file
	lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::table);

	auto file = resource::Open("gameflow.lua");
	if (!file.has_value()) {
		TOAST_ERROR("File gameflow.lua couldn't be open");
		return;
	}

	auto result = lua.script(*file);
	if (not result.valid()) {
		sol::error err = result;
		TOAST_WARN("gameflow.lua failed: {}", err.what());
		return;
	}

	sol::table table = result;
	world_list = table.as<std::vector<std::string>>();

	listener.Subscribe<toast::LoadWorld>([this](auto* e) {
		LoadWorld(e->world);
		return true;
	});
	listener.Subscribe<toast::LoadLevel>([this](auto* e) {
		LoadLevel(e->world, e->level);
		return true;
	});
	listener.Subscribe<toast::NextWorld>([this](auto* _) {
		NextWorld();
		return true;
	});
	listener.Subscribe<toast::NextLevel>([this](auto* _) {
		NextLevel();
		return true;
	});

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

	if (m.currentLevel.has_value()) {
		m.currentLevel->wait();
		auto* scene = toast::World::Get(m.currentLevel->get());
		scene->Nuke();
		m.currentLevel = std::nullopt;
	}

	if (m.nextLevel.has_value()) {
		m.nextLevel->wait();
		auto* scene = toast::World::Get(m.nextLevel->get());
		scene->Nuke();
		m.nextLevel = std::nullopt;
	}

	m.world = world;
	m.level = std::nullopt;

	sol::state lua;

	lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::table);

	auto file = resource::Open(m.worldList[world]);
	if (!file.has_value()) {
		TOAST_ERROR("File {} couldn't be open", m.worldList[world]);
		return;
	}

	auto result = lua.script(*file);
	if (not result.valid()) {
		sol::error err = result;
		TOAST_WARN("{} failed: {}", m.worldList[world], err.what());
		return;
	}

	sol::table table = result;
	m.levelList = table.as<std::vector<std::string>>();

	m.nextLevel = toast::World::LoadScene(m.levelList[0]);
}

void GameFlow::LoadLevel(unsigned world, unsigned level) {
	LoadWorld(world);
	if (m.level == level || m.levelList.size() <= level) {
		return;
	}
	m.level = level;

	m.currentLevel = toast::World::LoadScene(m.levelList[level]);
	if (m.levelList.size() >= level + 1) {
		m.nextLevel = toast::World::LoadScene(m.levelList[level + 1]);
	} else {
		m.nextLevel = std::nullopt;
	}
	m.currentLevel->wait();

	auto* scene = toast::World::Get(m.currentLevel->get());
	scene->enabled(true);
}

void GameFlow::NextLevel() {
	// Nuke Loaded Level
	if (m.currentLevel.has_value()) {
		auto* scene = toast::World::Get(m.currentLevel->get());
		scene->Nuke();
		m.currentLevel = std::nullopt;
	}

	// Increment Level
	// clang-format off
	m.level = m.level.transform([](unsigned val) {
    return val + 1;
  }).value_or(0);
	// clang-format on

	if (m.levelList.size() <= m.level.value() + 1) {
		TOAST_WARN("End Of the World...");
		m.level = std::nullopt;
		return;
	}

	// Load & Enable New Level
	m.currentLevel = std::move(m.nextLevel).or_else([this]() -> std::optional<std::shared_future<unsigned>> {
		return toast::World::LoadScene(m.levelList[m.level.value()]);
	});
	m.currentLevel->wait();
	auto* scene = toast::World::Get(m.currentLevel->get());
	scene->enabled(true);

	// Pre Load Next Level :3
	if (m.levelList.size() >= m.level.value() + 1) {
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
