#include "Toast/Objects/Scene.hpp"

#include "Toast/Resources/ResourceManager.hpp"

namespace toast {

void Scene::Load(json_t j, bool force_create) {
	if (j["format"].get<std::string>() != "scene") {
		throw ToastException("Json format is invalid, expected .scene");
	}
	Object::Load(j, force_create);
}

json_t Scene::Save() const {
	json_t j = Object::Save();

	if (!m_jsonPath.empty()) {
		m_jsonPath = "scenes/" + name() + ".scene";
	}
	j["format"] = "scene";
	j["file_path"] = m_jsonPath;

	// We're gonna save the scene in a file when we serialize it
	/// @todo: Find a better way of doing this
	// const std::string path = name() + ".scene";
	// resource::ResourceManager::SaveFile(path, j.dump(2));

	return j;
}

void Scene::Load(const std::string& json_path) {
	m_jsonPath = json_path;
	std::istringstream iss;
	json_t j;
	if (resource::Open(json_path, iss))
		iss >> j;
	else
		throw ToastException("Cannot open scene file: " + json_path);

	Load(j);
}

void Scene::Restart() {
	TOAST_INFO("Reloading scene {0}", name());
	json_t j;

	try {
		std::string raw_file = *resource::Open(m_jsonPath);
		j = json_t::parse(raw_file);
		for (auto c_json : j["children"]) {
			auto* c = children[c_json["name"].get<std::string>()];
			c->Load(c_json);
		}
		enabled(true);
	} catch (const std::exception& e) { TOAST_WARN("Cannot restart scene {0}: {1}", name(), e.what()); }
}

}
