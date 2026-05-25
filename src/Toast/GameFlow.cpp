#include "Toast/GameFlow.hpp"

#include "Toast/Audio/Audio.hpp"
#include "Toast/CoroutineHandler.hpp"
#include "Toast/GameEvents.hpp"
#include "Toast/Log.hpp"
#include "Toast/Objects/Scene.hpp"
#include "Toast/Renderer/HUD/HUDLayer.hpp"
#include "Toast/Resources/ResourceManager.hpp"
#include "Toast/WaitAsync.hpp"
#include "Toast/World.hpp"
#include "sol/forward.hpp"
#include "sol/state.hpp"

#include <exception>
#include <future>
#include <optional>
#include <stdexcept>

namespace toast {

Scene* GameFlow::currentScene = nullptr;
GameFlow* instance;

auto GameFlow::GetLevel() -> std::optional<unsigned> {
	return instance->m.level;
}

auto GameFlow::GetWorld() -> std::optional<unsigned> {
	return instance->m.world;
}

Scene* GameFlow::CurrentScene() {
	try {
		if (instance->m.currentLevel) {
			instance->m.currentLevel->wait();
			auto* scene = toast::World::Get(instance->m.currentLevel->get());
			return dynamic_cast<Scene*>(scene);
		}
	} catch (std::exception& e) { TOAST_ERROR("{}", e.what()); }
	return nullptr;
}

GameFlow::GameFlow() {
	std::vector<std::string> world_list;
	instance = this;

	// Loading the lua file
	sol::state lua;
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
		m.nextLevel = toast::World::LoadScene(m.levelList[0]);
		return true;
	});
	listener.Subscribe<toast::LoadLevel>([this](toast::LoadLevel* e) {
		this->LoadLevel(e->world, e->level);
		if (e->world == 1) {
			if (not audio::is_playing("event:/City")) {
				auto res = audio::play("event:/City");
			}

			if (audio::is_playing("event:/Port")) {
				auto res = audio::set_param("event:/Port", "param:/Level End", 1);
			}
		} else if (e->world == 2) {
			if (not audio::is_playing("event:/Port")) {
				auto res = audio::play("event:/Port");
			}

			if (audio::is_playing("event:/City")) {
				auto res = audio::set_param("event:/City", "param:/Level End", 1);
			}
		}
		return true;
	});
	listener.Subscribe<toast::NextWorld>([this](auto* _) {
		NextWorld();
		return true;
	});
	listener.Subscribe<toast::NextLevel>([this](auto* _) {
		[](GameFlow& flow) -> toast::CoroutineTask {
			renderer::HUD::HUDLayer::Get()->ExecuteJS("fadeIn()");
			co_await toast::WaitSeconds(0.6f);
			flow.NextLevel();
			if (flow.m.world && flow.m.world == 1) {
				if (not audio::is_playing("event:/City")) {
					auto res = audio::play("event:/City");
				}

				if (audio::is_playing("event:/Port")) {
					auto res = audio::set_param("event:/Port", "param:/Level End", 1);
				}
			} else if (flow.m.world && flow.m.world == 2) {
				if (not audio::is_playing("event:/Port")) {
					auto res = audio::play("event:/Port");
				}

				if (audio::is_playing("event:/City")) {
					auto res = audio::set_param("event:/City", "param:/Level End", 1);
				}
			}
				co_await toast::WaitSeconds(0.1f);
			renderer::HUD::HUDLayer::Get()->ExecuteJS("fadeOut()");
		}(*this);
		return true;
	});
	listener.Subscribe<toast::ResetGameFlow>([this](auto* _) {
		Reset();
		return true;
	});
	listener.Subscribe<toast::RestartLevel>([this](auto* _) {
		Restart();
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
		try {
			auto* scene = toast::World::Get(m.currentLevel->get());
			if (!scene) {
				throw std::runtime_error("Current Level Deleted By Another Existence");
			}
			scene->Nuke();
		} catch (std::exception& e) { TOAST_ERROR("{}", e.what()); }
		m.currentLevel = std::nullopt;
	}

	if (m.nextLevel.has_value()) {
		m.nextLevel->wait();
		try {
			auto* scene = toast::World::Get(m.nextLevel->get());
			if (!scene) {
				throw std::runtime_error("Current Level Deleted By Another Existence");
			}
			scene->Nuke();
		} catch (std::exception& e) { TOAST_ERROR("{}", e.what()); }
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
}

void GameFlow::LoadLevel(unsigned world, unsigned level) {
	LoadWorld(world);
	if (m.level == level || m.levelList.size() <= level) {
		return;
	}
	if (not m.level && m.world == world && level == 0 && m.nextLevel) {
		m.level = level;
		m.currentLevel = std::move(m.nextLevel);
		m.currentLevel->wait();
		try {
			auto* scene = toast::World::Get(m.currentLevel->get());
			if (!scene) {
				throw std::runtime_error("Current Level Deleted By Another Existence");
			}
			scene->enabled(true);
		} catch (std::exception& e) { TOAST_ERROR("{}", e.what()); }

		if (m.levelList.size() >= level + 1) {
			m.nextLevel = toast::World::LoadScene(m.levelList[level + 1]);
		} else {
			m.nextLevel = std::nullopt;
		}
		return;
	}

	m.level = level;

	if (m.currentLevel) {
		auto* scene = toast::World::Get(m.currentLevel->get());
		if (scene) {
			scene->Nuke();
		}
	}

	m.currentLevel = toast::World::LoadScene(m.levelList[level]);
	if (m.levelList.size() >= level + 1) {
		m.nextLevel = toast::World::LoadScene(m.levelList[level + 1]);
	} else {
		m.nextLevel = std::nullopt;
	}
	m.currentLevel->wait();

	try {
		auto* scene = toast::World::Get(m.currentLevel->get());
		if (!scene) {
			throw std::runtime_error("Current Level Deleted By Another Existence");
		}
		scene->enabled(true);
		currentScene = dynamic_cast<Scene*>(scene);
	} catch (std::exception& e) { TOAST_ERROR("{}", e.what()); }
}

void GameFlow::NextLevel() {
	// Nuke Loaded Level
	if (m.currentLevel.has_value()) {
		try {
			auto* scene = toast::World::Get(m.currentLevel->get());
			if (!scene) {
				throw std::runtime_error("Current Level Deleted By Another Existence");
			}
			scene->Nuke();
		} catch (std::exception& e) { TOAST_ERROR("{}", e.what()); }
		m.currentLevel = std::nullopt;
	}

	// Increment Level
	// clang-format off
	m.level = m.level.transform([](unsigned val) {
    return val + 1;
  }).value_or(0);
	// clang-format on

	if (m.levelList.size() <= m.level.value()) {
		TOAST_WARN("End Of the World...");
		m.level = std::nullopt;
		return;
	}

	// Load & Enable New Level
	m.currentLevel = std::move(m.nextLevel).or_else([this]() -> std::optional<std::shared_future<unsigned>> {
		return toast::World::LoadScene(m.levelList[m.level.value()]);
	});
	try {
		m.currentLevel->wait();
		auto* scene = toast::World::Get(m.currentLevel->get());
		if (!scene) {
			throw std::runtime_error("Current Level Deleted By Another Existence");
		}
		scene->enabled(true);
		currentScene = dynamic_cast<Scene*>(scene);
	} catch (std::exception& e) { TOAST_ERROR("{}", e.what()); }

	// Pre Load Next Level :3
	if (m.levelList.size() > m.level.value() + 1) {
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

void GameFlow::Reset() {
	TOAST_INFO("Resetting Gameflow Destroying levels...");
	try {
		if (m.currentLevel) {
			m.currentLevel->wait();
			auto* scene = toast::World::Get(m.currentLevel->get());
			if (scene) {
				scene->Nuke();
			}
		}
	} catch (std::exception& e) { TOAST_ERROR("{}", e.what()); }

	try {
		if (m.nextLevel) {
			m.nextLevel->wait();
			auto* scene = toast::World::Get(m.nextLevel->get());
			if (scene) {
				scene->Nuke();
			}
		}
	} catch (std::exception& e) { TOAST_ERROR("{}", e.what()); }
	m.nextLevel = std::nullopt;
	m.currentLevel = std::nullopt;
	m.level = std::nullopt;
	m.world = std::nullopt;
	m.levelList.clear();
}

void GameFlow::Restart() {
	if (!m.currentLevel) {
		TOAST_WARN("No Active Scene Abort Restart");
		return;
	}
	m.currentLevel->wait();
	auto* scene = toast::World::Get(m.currentLevel->get());
	scene->SoftLoad();
	toast::World::ScheduleBegin(scene);
}

auto GameFlow::GetLevelName(unsigned world, unsigned level) -> std::string {
	sol::state lua;

	lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::table);

	auto file = resource::Open(instance->m.worldList[world]);
	if (!file.has_value()) {
		TOAST_ERROR("File {} couldn't be open", instance->m.worldList[world]);
		return "NULL";
	}

	auto result = lua.script(*file);
	if (not result.valid()) {
		sol::error err = result;
		TOAST_WARN("{} failed: {}", instance->m.worldList[world], err.what());
		return "NULL";
	}

	sol::table table = result;
	return table.as<std::vector<std::string>>()[level];
}
}
