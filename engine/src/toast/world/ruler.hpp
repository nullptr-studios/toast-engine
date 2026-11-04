/**
 * @file ruler.hpp
 * @author dario
 * @date 30 Oct 2026
 */

#pragma once

#include "workspace_events.hpp"

#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace toast {

class Camera;

/// @brief Measurements the editor ruler tool holds in a workspace, drawn as debug lines
class Ruler {
public:
	static constexpr size_t k_max_segments = 64;

	/// First call starts a segment and the second commits it
	void place(glm::vec3 point);
	void setPreview(std::optional<glm::vec3> point);
	void cancelPending();
	void removeLast();
	void clear();

	[[nodiscard]]
	auto pending() const noexcept -> std::optional<glm::vec3> {
		return m_start;
	}

	[[nodiscard]]
	auto empty() const noexcept -> bool {
		return m_segments.empty() && !m_start.has_value();
	}

	/// Keeps only the axis @p point moved furthest along from the pending start
	[[nodiscard]]
	auto lockAxis(glm::vec3 point) const -> glm::vec3;

	void draw(const Camera& camera, glm::vec2 viewport, bool tool_active) const;

	/// Labels are projected into viewport pixels
	[[nodiscard]]
	auto snapshot(const Camera& camera, glm::vec2 viewport, bool tool_active) const -> event::RulerChanged;

	[[nodiscard]]
	static auto similar(const event::RulerChanged& a, const event::RulerChanged& b) -> bool;

private:
	struct Segment {
		glm::vec3 a {0.0f};
		glm::vec3 b {0.0f};
	};

	std::vector<Segment> m_segments;
	std::optional<glm::vec3> m_start;
	std::optional<glm::vec3> m_preview;
};

}
