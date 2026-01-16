/**
 * @file ProjectSettings.hpp
 * @author Xein <xgonip@gmail.com>
 * @date 10/09/25
 * @brief Contains the .toast data
 */

#pragma once

namespace toast {

struct Version final {
	[[nodiscard]]
	std::string get() const {
		return std::string("v" + std::to_string(m_major) + "." + std::to_string(m_minor)) + "." + std::string("." + std::to_string(m_patch));
	}

	[[nodiscard]]
	unsigned major() const {
		return m_major;
	}

	[[nodiscard]]
	unsigned minor() const {
		return m_minor;
	}

	[[nodiscard]]
	unsigned patch() const {
		return m_patch;
	}

	Version(unsigned major, unsigned minor, unsigned patch) : m_major(major), m_minor(minor), m_patch(patch) { }

private:
	unsigned m_major;
	unsigned m_minor;
	unsigned m_patch;
};

struct ProjectSettings final {
public:
	ProjectSettings();
	static std::string name();
	static Version version();
	static const std::vector<std::string>& input_layouts();
	static const float input_deadzone();

private:
	static ProjectSettings* m_instance;
	std::string m_projectName;
	Version m_version;
	std::vector<std::string> m_inputLayouts;
	float m_inputDeadzone = 0.2;
};

}
