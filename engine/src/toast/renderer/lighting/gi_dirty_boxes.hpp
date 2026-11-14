/// @file gi_dirty_boxes.hpp
/// @author dario
/// @date 10/11/2026

#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <span>
#include <unordered_map>
#include <vector>

namespace renderer {
struct RenderFrame;

class GiDirtyBoxes {
public:
	/// Past it every extra region grows the last box
	static constexpr uint32_t k_max_boxes = 64;

	struct Counts {
		uint32_t moved = 0;
		uint32_t appeared = 0;
		uint32_t edits = 0;
		/// Meters including bodies under the motion threshold so physics jitter shows up
		float largest_motion = 0.0f;
	};

	/// Every rendered frame so skipped updates still reach the next one
	void gather(const RenderFrame& frame, float motion_threshold);

	/// Low then high per box
	[[nodiscard]]
	auto boxes() const -> std::span<const glm::vec3> {
		return m_boxes;
	}

	/// @return false when edits were lost so the consumer must rebuild everything
	auto take(Counts& counts) -> bool;

	void clear() { m_boxes.clear(); }

private:
	std::vector<glm::vec3> m_boxes;
	std::unordered_map<uint64_t, glm::mat4> m_models;
	Counts m_counts;
	uint64_t m_region_generation = 0;
	bool m_known = true;
};

}
