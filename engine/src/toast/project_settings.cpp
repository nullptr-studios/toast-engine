#include "project_settings.hpp"

#include "toast/params/files.hpp"
#include "toast/params/project_params.hpp"

#include <toml++/toml.hpp>

namespace toast {

namespace detail {

struct ProjectGroups {
	params::ProjectParams project;
	params::GameplayParams gameplay;
	params::UIParams ui;
};

}

namespace {

template<typename T>
auto handleFrom(const params::AssetRef& asset) -> assets::Handle<T> {
	const std::string& uri = asset.uri;
	if (uri.empty()) {
		return {};
	}
	return {nullptr, toast::UID::make(), uri};
}

}

ProjectSettings::ProjectSettings(const std::filesystem::path& path, params::Files* files)
    : m_groups(std::make_unique<detail::ProjectGroups>()) {
	ZoneScoped;
	instance = this;

	if (files != nullptr) {
		files->load(m_groups->project);
		files->load(m_groups->gameplay);
		files->load(m_groups->ui);
	}

	m_databases = m_groups->project.databases;
	if (m_databases.empty()) {
		m_databases.emplace_back("assets");
	}
	m_max_tick_rate = m_groups->project.max_tick_rate;

	m_gameplay_settings.m_init_scene = handleFrom<assets::Prefab>(m_groups->gameplay.init_scene);
	m_gameplay_settings.m_player = handleFrom<assets::Prefab>(m_groups->gameplay.player);

	m_ui_settings.m_color_scheme = handleFrom<assets::ColorScheme>(m_groups->ui.color_scheme);
	m_ui_settings.m_languages = m_groups->ui.languages;
	if (m_ui_settings.m_languages.empty()) {
		m_ui_settings.m_languages.emplace_back("en");
	}

	if (path.empty() || !std::filesystem::exists(path)) {
		TOAST_WARN("ProjectSettings", "No project file found; using defaults");
		return;
	}

	try {
		auto table = toml::parse_file(path.string());

		m_name = table["name"].value_or<std::string>("Untitled");

		if (auto* v = table["version"].as_array()) {
			for (size_t i = 0; i < 3 && i < v->size(); ++i) {
				m_version[i] = v->at(i).value_or<unsigned>(0u);
			}
		}

		TOAST_INFO("ProjectSettings", "Loaded '{}' {} — {} database(s)", m_name, version(), m_databases.size());

	} catch (const std::exception& e) {
		TOAST_ERROR("ProjectSettings", "Failed to parse {}: {}; using defaults", path.string(), e.what());
		m_name = "Untitled";
		m_version = {1, 0, 0};
	}
}

ProjectSettings::~ProjectSettings() {
	if (instance == this) {
		instance = nullptr;
	}
}

}
