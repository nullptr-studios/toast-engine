/// @file gi_dirty_boxes.cpp
/// @author dario
/// @date 10/11/2026

#include "toast/renderer/lighting/gi_dirty_boxes.hpp"

#include "toast/renderer/core/vulkan_common.hpp"
#include "toast/renderer/render_frame.hpp"
#include "toast/renderer/voxel/voxel_gpu_storage.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <toast/voxel/voxel_constants.hpp>
#include <utility>

namespace renderer {

void GiDirtyBoxes::gather(const RenderFrame& frame, float motion_threshold) {
	ZoneScoped;
	const auto add = [&](glm::vec3 low, glm::vec3 high) {
		if (m_boxes.size() < 2 * k_max_boxes) {
			m_boxes.push_back(low);
			m_boxes.push_back(high);
			return;
		}
		auto last = m_boxes.end() - 2;
		last[0] = glm::min(last[0], low);
		last[1] = glm::max(last[1], high);
	};

	Counts& counts = m_counts;
	const float threshold = std::max(motion_threshold, 0.0f);

	for (const auto& proxy : frame.voxel_instances) {
		if (proxy.has_previous && proxy.model == proxy.previous_model) {
			continue;
		}
		const glm::vec3 previous = glm::vec3(proxy.previous_model * proxy.inverse_model * glm::vec4(proxy.bounds_center, 1.0f));
		if (proxy.has_previous) {
			const glm::mat3 change = glm::mat3(proxy.model) - glm::mat3(proxy.previous_model);
			const float spread =
			    std::sqrt(glm::dot(change[0], change[0]) + glm::dot(change[1], change[1]) + glm::dot(change[2], change[2]));
			const float scale = std::max(glm::length(glm::vec3(proxy.model[0])), 1e-6f);
			const float motion = glm::distance(proxy.bounds_center, previous) + (spread * proxy.bounds_radius / scale);
			counts.largest_motion = std::max(counts.largest_motion, motion);
			if (motion < threshold) {
				continue;
			}
			++counts.moved;
		} else {
			++counts.appeared;
		}
		const glm::vec3 radius(proxy.bounds_radius);
		add(glm::min(proxy.bounds_center, previous) - radius, glm::max(proxy.bounds_center, previous) + radius);
	}

	bool known = true;
	if (frame.voxel_storage) {
		const uint64_t generation = frame.voxel_storage->generation();
		if (generation != m_region_generation && m_region_generation != 0) {
			m_models.clear();
			for (const auto& proxy : frame.voxel_instances) {
				m_models.emplace(proxy.node_rid, proxy.model * glm::scale(glm::mat4(1.0f), glm::vec3(voxel::k_voxel_size)));
			}
			known = frame.voxel_storage->changeHistory().forEachSince(m_region_generation, [&](const VoxelRegion& region) {
				const auto found = m_models.find(region.node_rid);
				if (found == m_models.end()) {
					return;
				}
				++counts.edits;
				glm::vec3 low(std::numeric_limits<float>::max());
				glm::vec3 high(std::numeric_limits<float>::lowest());
				for (uint32_t corner = 0; corner < 8; ++corner) {
					const glm::vec3 local(
					    (corner & 1) != 0 ? region.max.x : region.min.x,
					    (corner & 2) != 0 ? region.max.y : region.min.y,
					    (corner & 4) != 0 ? region.max.z : region.min.z
					);
					const glm::vec3 world = glm::vec3(found->second * glm::vec4(local, 1.0f));
					low = glm::min(low, world);
					high = glm::max(high, world);
				}
				add(low, high);
			});
		}
		m_region_generation = generation;
	}
	m_known = m_known && known;
}

auto GiDirtyBoxes::take(Counts& counts) -> bool {
	counts = std::exchange(m_counts, Counts {});
	return std::exchange(m_known, true);
}

}
