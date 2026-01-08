#include "Toast/ProjectSettings.hpp"

#include "Toast/Log.hpp"
#include "Toast/Physics/PhysicsEvents.hpp"
#include "Toast/Resources/ResourceManager.hpp"

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

namespace toast {

ProjectSettings* ProjectSettings::m_instance = nullptr;

ProjectSettings::ProjectSettings() : m_version(0, 0, 0) {
	if (m_instance) {
		throw ToastException("Tried to create Project Settings but it already exists");
	}
	m_instance = this;

	std::string raw_file = *resource::Open("assets/project_settings.toast");
	YAML::Node config = YAML::Load(raw_file);

	if (config["format"].as<std::string>() != "projectData") {
		throw ToastException("Unexpected type for Project Settings");
	}

	m_projectName = config["projectName"].as<std::string>();
	m_version = { config["projectVersion"][0].as<unsigned>(), config["projectVersion"][1].as<unsigned>(), config["projectVersion"][2].as<unsigned>() };

	m_inputLayouts.resize(config["input"]["layouts"].size());
	for (int i = 0; i < m_inputLayouts.size(); i++) {
		m_inputLayouts[i] = config["input"]["layouts"][i].as<std::string>();
	}

	double gr_x = config["physics"]["gravity"][0].as<double>();
	double gr_y = config["physics"]["gravity"][1].as<double>();
	event::Send(new physics::UpdatePhysicsDefaults(
	    { gr_x, gr_y },
	    config["physics"]["positionCorrection"]["ptc"].as<double>(),
	    config["physics"]["positionCorrection"]["slop"].as<double>(),
	    config["physics"]["eps"].as<double>(),
	    config["physics"]["epsSmall"].as<double>(),
	    config["physics"]["iterationCount"].as<unsigned>()
	));
}

std::string ProjectSettings::name() {
	return m_instance->m_projectName;
}

Version ProjectSettings::version() {
	return m_instance->m_version;
}

const std::vector<std::string>& ProjectSettings::input_layouts() {
	return m_instance->m_inputLayouts;
}

}
