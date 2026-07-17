#include "project_settings.hpp"

#include <toast/log.hpp>
#include <toml++/toml.hpp>

namespace toast {

ProjectSettings::ProjectSettings(const std::filesystem::path& path) {
	instance = this;

	if (path.empty() || !std::filesystem::exists(path)) {
		TOAST_WARN("ProjectSettings", "No project file found; using defaults");
		m_databases = {"assets"};
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

		if (auto* dbs = table["databases"].as_array()) {
			for (const auto& db : *dbs) {
				if (auto str = db.value<std::string>()) {
					m_databases.push_back(*str);
				}
			}
		}
		if (m_databases.empty()) {
			m_databases.emplace_back("assets");
		}

		if (auto* gameplay = table["gameplay"].as_table()) {
			auto make_handle = [](std::string_view uri) -> assets::Handle<assets::Prefab> {
				return {nullptr, toast::UID::make(), uri};
			};
			if (auto uri = (*gameplay)["init_scene"].value<std::string>()) {
				m_gameplay_settings.m_init_scene = make_handle(*uri);
			}
			if (auto uri = (*gameplay)["player"].value<std::string>()) {
				m_gameplay_settings.m_player = make_handle(*uri);
			}
		}

		TOAST_INFO("ProjectSettings", "Loaded '{}' {} — {} database(s)", m_name, version(), m_databases.size());

	} catch (const std::exception& e) {
		TOAST_ERROR("ProjectSettings", "Failed to parse {}: {}; using defaults", path.string(), e.what());
		m_name = "Untitled";
		m_version = {1, 0, 0};
		m_databases = {"assets"};
	}
}

}
