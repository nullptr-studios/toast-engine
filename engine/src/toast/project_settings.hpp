/**
 * @file project_settings.hpp
 * @author Xein
 * @date 07 Jul 2026
 */

#pragma once
#include <array>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <toast/assets/types.hpp>
#include <toast/export.hpp>
#include <vector>

namespace toast {

class TOAST_API GameplaySettings {
public:
	auto initScene() const { return m_init_scene; }

	auto player() const { return m_player; }

private:
	friend class ProjectSettings;
	assets::AssetHandle<assets::Prefab> m_init_scene;
	assets::AssetHandle<assets::Prefab> m_player;
};

class TOAST_API ProjectSettings {
public:
	explicit ProjectSettings(std::filesystem::path path);
	static auto get() -> ProjectSettings* { return instance; }

	static auto name() -> std::string_view { return instance->m_name; }

	static auto version() -> std::string {
		return std::format("v{}.{}.{}", instance->m_version[0], instance->m_version[1], instance->m_version[2]);
	}

	/// @brief List of content database names
	static auto databases() -> const std::vector<std::string>& { return instance->m_databases; }

	static auto gameplaySettings() -> const GameplaySettings& { return instance->m_gameplay_settings; }

private:
	static inline ProjectSettings* instance = nullptr;
	std::string m_name;
	std::array<unsigned, 3> m_version {1, 0, 0};
	std::vector<std::string> m_databases;
	GameplaySettings m_gameplay_settings;
};

}
