/**
 * @file GlmJson.hpp
 * @author Xein <xgonip@gmail.com>
 * @date 9/30/25
 * @brief Allows to serialize vectors and quaternions
 */

#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

namespace nlohmann {
// glm::vec2
inline void to_json(ordered_json& j, const glm::vec2& v) {
	j = ordered_json { v.x, v.y };
}

inline void from_json(const ordered_json& j, glm::vec2& v) {
	v.x = j.at(0).get<float>();
	v.y = j.at(1).get<float>();
}

inline void to_json(ordered_json& j, const glm::uvec2& v) {
	j = ordered_json { v.x, v.y };
}

inline void from_json(const ordered_json& j, glm::uvec2& v) {
	v.x = j.at(0).get<float>();
	v.y = j.at(1).get<float>();
}

// glm::vec2
inline void to_json(ordered_json& j, const glm::dvec2& v) {
	j = ordered_json { v.x, v.y };
}

inline void from_json(const ordered_json& j, glm::dvec2& v) {
	v.x = j.at(0).get<float>();
	v.y = j.at(1).get<float>();
}

// glm::vec3
inline void to_json(ordered_json& j, const glm::vec3& v) {
	j = ordered_json { v.x, v.y, v.z };
}

inline void from_json(const ordered_json& j, glm::vec3& v) {
	v.x = j.at(0).get<float>();
	v.y = j.at(1).get<float>();
	v.z = j.at(2).get<float>();
}

// glm::vec4
inline void to_json(ordered_json& j, const glm::vec4& v) {
	j = ordered_json { v.x, v.y, v.z, v.w };
}

inline void from_json(const ordered_json& j, glm::vec4& v) {
	v.x = j.at(0).get<float>();
	v.y = j.at(1).get<float>();
	v.z = j.at(2).get<float>();
	v.w = j.at(3).get<float>();
}

// glm::quat (w, x, y, z order)
inline void to_json(ordered_json& j, const glm::quat& q) {
	j = ordered_json { q.w, q.x, q.y, q.z };    // Common convention: w first
}

inline void from_json(const ordered_json& j, glm::quat& q) {
	q.w = j.at(0).get<float>();
	q.x = j.at(1).get<float>();
	q.y = j.at(2).get<float>();
	q.z = j.at(3).get<float>();
}

// glm::mat4
inline void to_json(ordered_json& j, const glm::mat4& m) {
	j = ordered_json::array();
	for (int i = 0; i < 4; ++i) {
		ordered_json row = ordered_json::array();
		for (int j_col = 0; j_col < 4; ++j_col) {
			row.push_back(m[i][j_col]);
		}
		j.push_back(row);
	}
}

inline void from_json(const ordered_json& j, glm::mat4& m) {
	for (int i = 0; i < 4; ++i) {
		for (int j_col = 0; j_col < 4; ++j_col) {
			m[i][j_col] = j.at(i).at(j_col).get<float>();
		}
	}
}
}
