#include "manifold.hpp"

#include "voxel_query.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>
#include <utility>
#include <vector>

namespace physics {
namespace _detail {

auto boxFeature(const glm::vec3& point, const glm::vec3& half_extents) -> BoxFeature {
	BoxFeature result;
	uint8_t boundary_count = 0;

	for (uint8_t axis = 0; axis < 3; ++axis) {
		if (std::abs(std::abs(point[axis]) - half_extents[axis]) > contact_tolerance) {
			continue;
		}

		result.axis_mask |= static_cast<uint8_t>(1U << axis);
		if (point[axis] >= 0.0f) {
			result.positive_mask |= static_cast<uint8_t>(1U << axis);
		}
		++boundary_count;
	}

	switch (boundary_count) {
		case 0: result.type = BoxFeatureType::interior; break;
		case 1: result.type = BoxFeatureType::face; break;
		case 2: result.type = BoxFeatureType::edge; break;
		default: result.type = BoxFeatureType::vertex; break;
	}
	return result;
}

auto contactFeature(const BoxFeature& feature) -> ContactFeatureID {
	if (feature.type == BoxFeatureType::vertex) {
		return boxVertexFeature(feature.positive_mask);
	}

	if (feature.type == BoxFeatureType::face) {
		for (int axis = 0; axis < 3; ++axis) {
			if ((feature.axis_mask & (1u << axis)) != 0) {
				return boxFaceFeature(axis, (feature.positive_mask & (1u << axis)) != 0);
			}
		}
	}

	if (feature.type == BoxFeatureType::edge) {
		for (int axis = 0; axis < 3; ++axis) {
			if ((feature.axis_mask & (1u << axis)) == 0) {
				return boxEdgeFeature(axis, feature.positive_mask);
			}
		}
	}

	return {};
}

auto worldSphere(const Body& body, const SphereShape& sphere) -> WorldSphere {
	return {
	  .center = body.position + body.rotation * sphere.local_center,
	  .radius = sphere.radius,
	};
}

auto worldCapsule(const Body& body, const CapsuleShape& capsule) -> WorldCapsule {
	const glm::vec3 center = body.position + body.rotation * capsule.local_center;
	glm::quat rotation = body.rotation * capsule.local_rotation;
	const float rotation_length_squared = glm::dot(rotation, rotation);
	rotation = std::isfinite(rotation_length_squared) && rotation_length_squared > direction_epsilon_sq
	               ? rotation / std::sqrt(rotation_length_squared)
	               : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	const glm::vec3 axis = rotation * glm::vec3 {0.0f, 0.0f, 1.0f};
	const float shaft_half_length = (0.5f * capsule.height) - capsule.radius;

	return {
	  .point_a = center - axis * shaft_half_length,
	  .point_b = center + axis * shaft_half_length,
	  .radius = capsule.radius,
	};
}

auto worldBox(const Body& body, const BoxShape& box) -> WorldBox {
	glm::quat rotation = body.rotation * box.local_rotation;
	const float rotation_length_squared = glm::dot(rotation, rotation);
	rotation = std::isfinite(rotation_length_squared) && rotation_length_squared > direction_epsilon_sq
	               ? rotation / std::sqrt(rotation_length_squared)
	               : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	return {
	  .center = body.position + body.rotation * box.local_center,
	  .rotation = glm::mat3_cast(rotation),
	  .half_extents = box.size * 0.5f,
	};
}

auto closestPointOnSegment(const glm::vec3& point, const glm::vec3& segment_a, const glm::vec3& segment_b)
    -> SegmentClosestPoint {
	const glm::vec3 segment = segment_b - segment_a;
	const float length_squared = glm::dot(segment, segment);
	if (length_squared <= direction_epsilon_sq) {
		return {.point = segment_a, .parameter = 0.0f};
	}

	const float parameter = std::clamp(glm::dot(point - segment_a, segment) / length_squared, 0.0f, 1.0f);
	return {.point = segment_a + segment * parameter, .parameter = parameter};
}

auto closestPointsBetweenSegments(const glm::vec3& a0, const glm::vec3& a1, const glm::vec3& b0, const glm::vec3& b1)
    -> SegmentClosestPoints {
	const glm::vec3 direction_a = a1 - a0;
	const glm::vec3 direction_b = b1 - b0;
	const glm::vec3 start_delta = a0 - b0;
	const float length_a_squared = glm::dot(direction_a, direction_a);
	const float length_b_squared = glm::dot(direction_b, direction_b);

	if (length_a_squared <= direction_epsilon_sq && length_b_squared <= direction_epsilon_sq) {
		return {.point_a = a0, .point_b = b0, .parameter_a = 0.0f, .parameter_b = 0.0f};
	}
	if (length_a_squared <= direction_epsilon_sq) {
		const SegmentClosestPoint closest = closestPointOnSegment(a0, b0, b1);
		return {.point_a = a0, .point_b = closest.point, .parameter_a = 0.0f, .parameter_b = closest.parameter};
	}
	if (length_b_squared <= direction_epsilon_sq) {
		const SegmentClosestPoint closest = closestPointOnSegment(b0, a0, a1);
		return {.point_a = closest.point, .point_b = b0, .parameter_a = closest.parameter, .parameter_b = 0.0f};
	}

	const glm::vec3 directions_cross = glm::cross(direction_a, direction_b);
	const glm::vec3 offset_cross = glm::cross(start_delta, direction_a);
	const float parallel_scale = length_a_squared * length_b_squared;
	if (glm::dot(directions_cross, directions_cross) <= parallel_axis_epsilon_sq * parallel_scale &&
	    glm::dot(offset_cross, offset_cross) <= parallel_axis_epsilon_sq * length_a_squared) {
		const float b0_on_a = glm::dot(b0 - a0, direction_a) / length_a_squared;
		const float b1_on_a = glm::dot(b1 - a0, direction_a) / length_a_squared;
		const float overlap_start = std::max(0.0f, std::min(b0_on_a, b1_on_a));
		const float overlap_end = std::min(1.0f, std::max(b0_on_a, b1_on_a));
		if (overlap_start <= overlap_end) {
			const float parameter_a = 0.5f * (overlap_start + overlap_end);
			const glm::vec3 point = a0 + direction_a * parameter_a;
			const float parameter_b = std::clamp(glm::dot(point - b0, direction_b) / length_b_squared, 0.0f, 1.0f);
			return {.point_a = point, .point_b = point, .parameter_a = parameter_a, .parameter_b = parameter_b};
		}
	}

	const float a = length_a_squared;
	const float e = length_b_squared;
	const float b = glm::dot(direction_a, direction_b);
	const float c = glm::dot(direction_a, start_delta);
	const float f = glm::dot(direction_b, start_delta);
	const float denominator = (a * e) - (b * b);
	float parameter_a =
	    denominator > parallel_axis_epsilon_sq * a * e ? std::clamp(((b * f) - (c * e)) / denominator, 0.0f, 1.0f) : 0.0f;
	float parameter_b = ((b * parameter_a) + f) / e;

	if (parameter_b < 0.0f) {
		parameter_b = 0.0f;
		parameter_a = std::clamp(-c / a, 0.0f, 1.0f);
	} else if (parameter_b > 1.0f) {
		parameter_b = 1.0f;
		parameter_a = std::clamp((b - c) / a, 0.0f, 1.0f);
	}

	return {
	  .point_a = a0 + direction_a * parameter_a,
	  .point_b = b0 + direction_b * parameter_b,
	  .parameter_a = parameter_a,
	  .parameter_b = parameter_b,
	};
}

auto closestPointOnBox(const glm::vec3& point, const WorldBox& box) -> glm::vec3 {
	const glm::vec3 local_point = glm::transpose(box.rotation) * (point - box.center);
	const glm::vec3 local_closest = glm::clamp(local_point, -box.half_extents, box.half_extents);
	return box.center + box.rotation * local_closest;
}

auto closestPointsSegmentBox(const glm::vec3& segment_a, const glm::vec3& segment_b, const WorldBox& box)
    -> SegmentBoxClosestPoints {
	const glm::mat3 inverse_rotation = glm::transpose(box.rotation);
	const glm::vec3 local_a = inverse_rotation * (segment_a - box.center);
	const glm::vec3 local_b = inverse_rotation * (segment_b - box.center);
	const glm::vec3 direction = local_b - local_a;

	std::vector<float> interval_ends {0.0f, 1.0f};
	interval_ends.reserve(8);
	for (uint8_t axis = 0; axis < 3; ++axis) {
		if (std::abs(direction[axis]) <= std::sqrt(direction_epsilon_sq)) {
			continue;
		}
		for (const float side : std::array {-box.half_extents[axis], box.half_extents[axis]}) {
			const float parameter = (side - local_a[axis]) / direction[axis];
			if (parameter > 0.0f && parameter < 1.0f) {
				interval_ends.emplace_back(parameter);
			}
		}
	}
	std::ranges::sort(interval_ends);
	const auto unique_end = std::ranges::unique(interval_ends, [](float lhs, float rhs) {
		                        return std::abs(lhs - rhs) <= std::numeric_limits<float>::epsilon();
	                        }).begin();
	interval_ends.erase(unique_end, interval_ends.end());

	float best_parameter = 0.0f;
	float best_distance_squared = std::numeric_limits<float>::infinity();
	glm::vec3 best_segment_point = local_a;
	glm::vec3 best_box_point = glm::clamp(local_a, -box.half_extents, box.half_extents);

	const auto test_parameter = [&](float parameter) {
		const glm::vec3 segment_point = local_a + direction * parameter;
		const glm::vec3 box_point = glm::clamp(segment_point, -box.half_extents, box.half_extents);
		const glm::vec3 delta = box_point - segment_point;
		const float distance_squared = glm::dot(delta, delta);
		if (distance_squared < best_distance_squared || (distance_squared == best_distance_squared && parameter < best_parameter)) {
			best_parameter = parameter;
			best_distance_squared = distance_squared;
			best_segment_point = segment_point;
			best_box_point = box_point;
		}
	};

	for (size_t interval = 0; interval + 1 < interval_ends.size(); ++interval) {
		const float interval_start = interval_ends[interval];
		const float interval_end = interval_ends[interval + 1];
		const float midpoint = 0.5f * (interval_start + interval_end);
		float numerator = 0.0f;
		float denominator = 0.0f;

		for (uint8_t axis = 0; axis < 3; ++axis) {
			const float midpoint_coordinate = local_a[axis] + (direction[axis] * midpoint);
			float offset = 0.0f;
			if (midpoint_coordinate < -box.half_extents[axis]) {
				offset = local_a[axis] + box.half_extents[axis];
			} else if (midpoint_coordinate > box.half_extents[axis]) {
				offset = local_a[axis] - box.half_extents[axis];
			} else {
				continue;
			}
			numerator += offset * direction[axis];
			denominator += direction[axis] * direction[axis];
		}

		test_parameter(interval_start);
		test_parameter(interval_end);
		if (denominator > direction_epsilon_sq) {
			test_parameter(std::clamp(-numerator / denominator, interval_start, interval_end));
		}
	}

	return {
	  .segment_point = box.center + box.rotation * best_segment_point,
	  .box_point = box.center + box.rotation * best_box_point,
	  .segment_parameter = best_parameter,
	  .box_feature = boxFeature(best_box_point, box.half_extents),
	};
}

auto perpendicularTo(const glm::vec3& segment) -> glm::vec3 {
	const float length_squared = glm::dot(segment, segment);
	if (!std::isfinite(length_squared) || length_squared <= direction_epsilon_sq) {
		return {1.0f, 0.0f, 0.0f};
	}
	const glm::vec3 axis = segment / std::sqrt(length_squared);
	const glm::vec3 absolute_axis = glm::abs(axis);
	glm::vec3 reference;

	if (absolute_axis.x <= absolute_axis.y && absolute_axis.x <= absolute_axis.z) {
		reference = {1.0f, 0.0f, 0.0f};
	} else if (absolute_axis.y <= absolute_axis.z) {
		reference = {0.0f, 1.0f, 0.0f};
	} else {
		reference = {0.0f, 0.0f, 1.0f};
	}

	glm::vec3 perpendicular = glm::cross(axis, reference);
	float perpendicular_length_squared = glm::dot(perpendicular, perpendicular);
	if (!std::isfinite(perpendicular_length_squared) || perpendicular_length_squared <= direction_epsilon_sq) {
		return {1.0f, 0.0f, 0.0f};
	}
	return perpendicular / std::sqrt(perpendicular_length_squared);
}

auto combineMaterials(const PhysicsMaterial& a, const PhysicsMaterial& b) -> ContactMaterial {
	return {
	  .restitution = (a.restitution + b.restitution) * 0.5f,
	  .static_friction = (a.static_friction + b.static_friction) * 0.5f,
	  .dynamic_friction = (a.dynamic_friction + b.dynamic_friction) * 0.5f,
	};
}

auto canonicalPairAxis(const BroadPhasePair& pair) -> glm::vec3 {
	uint64_t selector =
	    static_cast<uint64_t>(pair.a.body.slot) + pair.a.shape.slot + static_cast<uint64_t>(pair.b.body.slot) + pair.b.shape.slot;
	glm::vec3 axis {};
	axis[selector % 3] = pair.b < pair.a ? -1.0f : 1.0f;
	return axis;
}

enum class BoxAxisType : std::uint8_t {
	face_a,
	face_b,
	edge
};

struct BoxSatResult {
	glm::vec3 axis {};
	float penetration = std::numeric_limits<float>::infinity();
	BoxAxisType type = BoxAxisType::face_a;
	int axis_a = 0;
	int axis_b = 0;
};

struct ContactCandidate {
	glm::vec3 position {};
	float penetration = 0.0f;
	ContactFeatureID feature_a {};
	ContactFeatureID feature_b {};
};

struct BoxSupportPoint {
	glm::vec3 position {};
	ContactFeatureID feature {};
};

struct BoxSupportEdge {
	std::array<glm::vec3, 2> points {};
	ContactFeatureID feature {};
};

struct BoxClipVertex {
	glm::vec3 position {};
	ContactFeatureID incident_feature {};
	uint8_t positive_mask = 0;
	bool original_vertex = false;
	bool clipped = false;
	int clip_axis = 0;
	bool clip_positive = false;
};

template<typename T, size_t Capacity>
class FixedBuffer {
public:
	[[nodiscard]]
	auto begin() noexcept {
		return m_values.begin();
	}

	[[nodiscard]]
	auto end() noexcept {
		return m_values.begin() + static_cast<std::ptrdiff_t>(m_size);
	}

	[[nodiscard]]
	auto begin() const noexcept {
		return m_values.begin();
	}

	[[nodiscard]]
	auto end() const noexcept {
		return m_values.begin() + static_cast<std::ptrdiff_t>(m_size);
	}

	[[nodiscard]]
	auto back() const -> const T& {
		return m_values[m_size - 1];
	}

	[[nodiscard]]
	auto empty() const noexcept -> bool {
		return m_size == 0;
	}

	void emplaceBack(const T& value) {
		TOAST_ASSERT(m_size < Capacity, "Physics", "Fixed collision buffer capacity exceeded");
		if (m_size < Capacity) {
			m_values[m_size++] = value;
		}
	}

private:
	std::array<T, Capacity> m_values {};
	size_t m_size = 0;
};

auto boxProjectionRadius(const WorldBox& box, const glm::vec3& axis) -> float {
	float radius = 0.0f;
	for (int index = 0; index < 3; ++index) {
		radius += box.half_extents[index] * std::abs(glm::dot(box.rotation[index], axis));
	}
	return radius;
}

auto boxSupportPoint(const WorldBox& box, const glm::vec3& direction) -> BoxSupportPoint {
	glm::vec3 point = box.center;
	uint8_t positive_mask = 0;
	for (int axis = 0; axis < 3; ++axis) {
		float sign = glm::dot(box.rotation[axis], direction) >= 0.0f ? 1.0f : -1.0f;
		point += box.rotation[axis] * box.half_extents[axis] * sign;
		if (sign > 0.0f) {
			positive_mask |= static_cast<uint8_t>(1u << axis);
		}
	}
	return {.position = point, .feature = boxVertexFeature(positive_mask)};
}

auto boxSupportEdge(const WorldBox& box, int edge_axis, const glm::vec3& direction) -> BoxSupportEdge {
	glm::vec3 edge_center = box.center;
	uint8_t positive_mask = 0;
	for (int axis = 0; axis < 3; ++axis) {
		if (axis == edge_axis) {
			continue;
		}
		float sign = glm::dot(box.rotation[axis], direction) >= 0.0f ? 1.0f : -1.0f;
		edge_center += box.rotation[axis] * box.half_extents[axis] * sign;
		if (sign > 0.0f) {
			positive_mask |= static_cast<uint8_t>(1u << axis);
		}
	}

	glm::vec3 edge_offset = box.rotation[edge_axis] * box.half_extents[edge_axis];
	return {
	  .points = {edge_center - edge_offset, edge_center + edge_offset},
	  .feature = boxEdgeFeature(edge_axis, positive_mask),
	};
}

auto clippedIncidentFeature(const BoxClipVertex& first, const BoxClipVertex& second) -> ContactFeatureID {
	if (first.original_vertex && second.original_vertex) {
		uint8_t different_axis = first.positive_mask ^ second.positive_mask;
		for (int axis = 0; axis < 3; ++axis) {
			if (different_axis == (1u << axis)) {
				return boxEdgeFeature(axis, first.positive_mask & static_cast<uint8_t>(~different_axis));
			}
		}
	}

	return first.incident_feature.value <= second.incident_feature.value ? first.incident_feature : second.incident_feature;
}

auto clipPolygonAgainstPlane(
    FixedBuffer<BoxClipVertex, 8> polygon, const glm::vec3& plane_center, const glm::vec3& plane_normal, float plane_extent,
    int plane_axis, bool plane_positive
) -> FixedBuffer<BoxClipVertex, 8> {
	FixedBuffer<BoxClipVertex, 8> result;
	if (polygon.empty()) {
		return result;
	}

	BoxClipVertex previous = polygon.back();
	float previous_distance = plane_extent - glm::dot(previous.position - plane_center, plane_normal);
	bool previous_inside = previous_distance >= -contact_tolerance;

	for (const BoxClipVertex& current : polygon) {
		float current_distance = plane_extent - glm::dot(current.position - plane_center, plane_normal);
		bool current_inside = current_distance >= -contact_tolerance;

		if (current_inside != previous_inside) {
			float denominator = previous_distance - current_distance;
			if (std::abs(denominator) > 1.0e-8f) {
				float amount = previous_distance / denominator;
				result.emplaceBack(
				    BoxClipVertex {
				      .position = previous.position + (current.position - previous.position) * amount,
				      .incident_feature = clippedIncidentFeature(previous, current),
				      .clipped = true,
				      .clip_axis = plane_axis,
				      .clip_positive = plane_positive,
				    }
				);
			}
		}

		if (current_inside) {
			result.emplaceBack(current);
		}

		previous = current;
		previous_distance = current_distance;
		previous_inside = current_inside;
	}

	return result;
}

auto incidentFaceVertices(const WorldBox& box, const glm::vec3& reference_normal) -> FixedBuffer<BoxClipVertex, 8> {
	int face_axis = 0;
	float greatest_alignment = -1.0f;
	for (int axis = 0; axis < 3; ++axis) {
		float alignment = std::abs(glm::dot(box.rotation[axis], reference_normal));
		if (alignment > greatest_alignment) {
			greatest_alignment = alignment;
			face_axis = axis;
		}
	}

	float face_sign = glm::dot(box.rotation[face_axis], reference_normal) > 0.0f ? -1.0f : 1.0f;
	int tangent_a = (face_axis + 1) % 3;
	int tangent_b = (face_axis + 2) % 3;
	glm::vec3 face_center = box.center + box.rotation[face_axis] * box.half_extents[face_axis] * face_sign;
	glm::vec3 offset_a = box.rotation[tangent_a] * box.half_extents[tangent_a];
	glm::vec3 offset_b = box.rotation[tangent_b] * box.half_extents[tangent_b];

	auto vertex = [&](float sign_a, float sign_b) {
		uint8_t positive_mask = 0;
		if (face_sign > 0.0f) {
			positive_mask |= static_cast<uint8_t>(1u << face_axis);
		}
		if (sign_a > 0.0f) {
			positive_mask |= static_cast<uint8_t>(1u << tangent_a);
		}
		if (sign_b > 0.0f) {
			positive_mask |= static_cast<uint8_t>(1u << tangent_b);
		}

		return BoxClipVertex {
		  .position = face_center + offset_a * sign_a + offset_b * sign_b,
		  .incident_feature = boxVertexFeature(positive_mask),
		  .positive_mask = positive_mask,
		  .original_vertex = true,
		};
	};

	FixedBuffer<BoxClipVertex, 8> result;
	result.emplaceBack(vertex(-1.0f, -1.0f));
	result.emplaceBack(vertex(1.0f, -1.0f));
	result.emplaceBack(vertex(1.0f, 1.0f));
	result.emplaceBack(vertex(-1.0f, 1.0f));
	return result;
}

auto reduceContacts(std::vector<ContactCandidate> candidates, const glm::vec3& normal) -> std::vector<ContactCandidate> {
	std::erase_if(candidates, [](const ContactCandidate& candidate) {
		const bool position_is_finite =
		    std::isfinite(candidate.position.x) && std::isfinite(candidate.position.y) && std::isfinite(candidate.position.z);
		return !position_is_finite || !std::isfinite(candidate.penetration) || candidate.penetration < -contact_tolerance;
	});
	for (ContactCandidate& candidate : candidates) {
		candidate.penetration = std::max(candidate.penetration, 0.0f);
	}

	glm::vec3 tangent_u = perpendicularTo(normal);
	glm::vec3 tangent_v = glm::cross(normal, tangent_u);
	auto contact_less = [&](const ContactCandidate& lhs, const ContactCandidate& rhs) {
		if (lhs.feature_a != rhs.feature_a) {
			return lhs.feature_a < rhs.feature_a;
		}
		if (lhs.feature_b != rhs.feature_b) {
			return lhs.feature_b < rhs.feature_b;
		}

		float lhs_u = glm::dot(lhs.position, tangent_u);
		float rhs_u = glm::dot(rhs.position, tangent_u);
		if (lhs_u != rhs_u) {
			return lhs_u < rhs_u;
		}
		return glm::dot(lhs.position, tangent_v) < glm::dot(rhs.position, tangent_v);
	};
	std::ranges::sort(candidates, contact_less);

	std::vector<ContactCandidate> unique_candidates;
	unique_candidates.reserve(candidates.size());
	for (const ContactCandidate& candidate : candidates) {
		auto duplicate = std::ranges::find_if(unique_candidates, [&](const ContactCandidate& existing) {
			glm::vec3 difference = existing.position - candidate.position;
			return glm::dot(difference, difference) <= duplicate_point_epsilon_sq;
		});
		if (duplicate == unique_candidates.end()) {
			unique_candidates.emplace_back(candidate);
		} else {
			duplicate->penetration = std::max(duplicate->penetration, candidate.penetration);
		}
	}
	candidates = std::move(unique_candidates);

	if (candidates.size() <= 4) {
		return candidates;
	}

	std::vector<size_t> selected;
	selected.reserve(4);

	auto choose = [&](auto score) {
		size_t best_index = 0;
		float best_score = -std::numeric_limits<float>::infinity();
		for (size_t index = 0; index < candidates.size(); ++index) {
			if (std::ranges::find(selected, index) != selected.end()) {
				continue;
			}
			float candidate_score = score(index);
			if (candidate_score > best_score) {
				best_score = candidate_score;
				best_index = index;
			}
		}
		selected.emplace_back(best_index);
	};

	choose([&](size_t index) { return candidates[index].penetration; });
	choose([&](size_t index) {
		glm::vec3 delta = candidates[index].position - candidates[selected[0]].position;
		return glm::dot(delta, delta);
	});
	choose([&](size_t index) {
		glm::vec3 first = candidates[selected[1]].position - candidates[selected[0]].position;
		glm::vec3 second = candidates[index].position - candidates[selected[0]].position;
		glm::vec3 area = glm::cross(first, second);
		return glm::dot(area, area);
	});
	choose([&](size_t index) {
		auto triangle_area = [&](size_t first, size_t second, size_t third) {
			glm::vec3 side_a = candidates[second].position - candidates[first].position;
			glm::vec3 side_b = candidates[third].position - candidates[first].position;
			return std::sqrt(glm::dot(glm::cross(side_a, side_b), glm::cross(side_a, side_b)));
		};
		return triangle_area(selected[0], selected[1], index) + triangle_area(selected[1], selected[2], index) +
		       triangle_area(selected[2], selected[0], index);
	});

	std::vector<ContactCandidate> result;
	result.reserve(4);
	for (size_t index : selected) {
		result.emplace_back(candidates[index]);
	}
	std::ranges::sort(result, contact_less);
	return result;
}

struct BoxSatContacts {
	glm::vec3 normal;
	FixedBuffer<ContactCandidate, 8> candidates;
};

auto boxSatContactsFromAxis(
    const WorldBox& box_a, const WorldBox& box_b, const BoxSatResult& best_axis, const glm::vec3& center_delta
) -> BoxSatContacts;

auto collideWorldBoxes(const WorldBox& box_a, const WorldBox& box_b) -> std::optional<BoxSatContacts> {
	glm::vec3 center_delta = box_b.center - box_a.center;
	BoxSatResult best_axis;
	bool separated = false;

	// A negative hint means compute the projection radius the general way since extent is never negative
	auto test_axis =
	    [&](glm::vec3 axis, BoxAxisType type, int axis_a, int axis_b, float radius_a_hint = -1.0f, float radius_b_hint = -1.0f) {
		    float length_squared = glm::dot(axis, axis);
		    if (length_squared <= parallel_axis_epsilon_sq) {
			    return;
		    }

		    axis /= std::sqrt(length_squared);
		    float radius_a = radius_a_hint >= 0.0f ? radius_a_hint : boxProjectionRadius(box_a, axis);
		    float radius_b = radius_b_hint >= 0.0f ? radius_b_hint : boxProjectionRadius(box_b, axis);
		    float center_distance = std::abs(glm::dot(center_delta, axis));
		    float overlap = radius_a + radius_b - center_distance;
		    if (overlap < -contact_tolerance) {
			    separated = true;
			    return;
		    }

		    float penetration = std::max(overlap, 0.0f);
		    bool new_is_face = type != BoxAxisType::edge;
		    bool old_is_face = best_axis.type != BoxAxisType::edge;
		    bool better = penetration < best_axis.penetration - contact_tolerance;
		    bool nearly_equal = std::abs(penetration - best_axis.penetration) <= contact_tolerance;
		    if (better || (nearly_equal && new_is_face && !old_is_face)) {
			    best_axis.axis = axis;
			    best_axis.penetration = penetration;
			    best_axis.type = type;
			    best_axis.axis_a = axis_a;
			    best_axis.axis_b = axis_b;
		    }
	    };

	// A box own face axis is an orthonormal rotation column so its own projection radius there is its half extent
	for (int axis = 0; axis < 3; ++axis) {
		test_axis(box_a.rotation[axis], BoxAxisType::face_a, axis, 0, box_a.half_extents[axis]);
		if (separated) {
			return std::nullopt;
		}
	}

	for (int axis = 0; axis < 3; ++axis) {
		test_axis(box_b.rotation[axis], BoxAxisType::face_b, 0, axis, -1.0f, box_b.half_extents[axis]);
		if (separated) {
			return std::nullopt;
		}
	}

	for (int axis_a = 0; axis_a < 3; ++axis_a) {
		for (int axis_b = 0; axis_b < 3; ++axis_b) {
			glm::vec3 axis = glm::cross(box_a.rotation[axis_a], box_b.rotation[axis_b]);
			test_axis(axis, BoxAxisType::edge, axis_a, axis_b);
			if (separated) {
				return std::nullopt;
			}
		}
	}

	return boxSatContactsFromAxis(box_a, box_b, best_axis, center_delta);
}

/// Builds contacts from an already chosen separating axis shared by every box vs box SAT variant
auto boxSatContactsFromAxis(
    const WorldBox& box_a, const WorldBox& box_b, const BoxSatResult& best_axis, const glm::vec3& center_delta
) -> BoxSatContacts {
	glm::vec3 normal = best_axis.axis;
	float normal_direction = glm::dot(center_delta, normal);
	if (normal_direction < 0.0f) {
		normal = -normal;
	} else if (std::abs(normal_direction) <= 1.0e-8f) {
		for (int component = 0; component < 3; ++component) {
			if (std::abs(normal[component]) <= 1.0e-8f) {
				continue;
			}
			if (normal[component] < 0.0f) {
				normal = -normal;
			}
			break;
		}
	}

	FixedBuffer<ContactCandidate, 8> candidates;
	if (best_axis.type == BoxAxisType::edge) {
		auto edge_a = boxSupportEdge(box_a, best_axis.axis_a, normal);
		auto edge_b = boxSupportEdge(box_b, best_axis.axis_b, -normal);
		auto closest = closestPointsBetweenSegments(edge_a.points[0], edge_a.points[1], edge_b.points[0], edge_b.points[1]);
		candidates.emplaceBack(
		    ContactCandidate {
		      .position = (closest.point_a + closest.point_b) * 0.5f,
		      .penetration = best_axis.penetration,
		      .feature_a = edge_a.feature,
		      .feature_b = edge_b.feature,
		    }
		);
	} else {
		bool reference_is_a = best_axis.type == BoxAxisType::face_a;
		WorldBox reference = reference_is_a ? box_a : box_b;
		WorldBox incident = reference_is_a ? box_b : box_a;
		int reference_axis = reference_is_a ? best_axis.axis_a : best_axis.axis_b;
		glm::vec3 reference_normal = reference_is_a ? normal : -normal;
		float reference_sign = glm::dot(reference.rotation[reference_axis], reference_normal) >= 0.0f ? 1.0f : -1.0f;
		glm::vec3 reference_face_center =
		    reference.center + reference.rotation[reference_axis] * reference.half_extents[reference_axis] * reference_sign;
		int tangent_a = (reference_axis + 1) % 3;
		int tangent_b = (reference_axis + 2) % 3;
		FixedBuffer<BoxClipVertex, 8> polygon = incidentFaceVertices(incident, reference_normal);

		polygon = clipPolygonAgainstPlane(
		    polygon, reference_face_center, reference.rotation[tangent_a], reference.half_extents[tangent_a], tangent_a, true
		);
		polygon = clipPolygonAgainstPlane(
		    polygon, reference_face_center, -reference.rotation[tangent_a], reference.half_extents[tangent_a], tangent_a, false
		);
		polygon = clipPolygonAgainstPlane(
		    polygon, reference_face_center, reference.rotation[tangent_b], reference.half_extents[tangent_b], tangent_b, true
		);
		polygon = clipPolygonAgainstPlane(
		    polygon, reference_face_center, -reference.rotation[tangent_b], reference.half_extents[tangent_b], tangent_b, false
		);

		for (const BoxClipVertex& incident_vertex : polygon) {
			float separation = glm::dot(incident_vertex.position - reference_face_center, reference_normal);
			if (separation > contact_tolerance) {
				continue;
			}

			glm::vec3 reference_point = incident_vertex.position - reference_normal * separation;
			glm::vec3 contact_position = (incident_vertex.position + reference_point) * 0.5f;
			float penetration = std::max(-separation, 0.0f);
			ContactFeatureID reference_feature =
			    incident_vertex.clipped
			        ? boxClipFeature(reference_axis, reference_sign > 0.0f, incident_vertex.clip_axis, incident_vertex.clip_positive)
			        : boxFaceFeature(reference_axis, reference_sign > 0.0f);
			ContactCandidate new_candidate {
			  .position = contact_position,
			  .penetration = penetration,
			  .feature_a = reference_is_a ? reference_feature : incident_vertex.incident_feature,
			  .feature_b = reference_is_a ? incident_vertex.incident_feature : reference_feature,
			};
			bool duplicate = false;
			for (auto& candidate : candidates) {
				glm::vec3 difference = candidate.position - contact_position;
				if (glm::dot(difference, difference) <= duplicate_point_epsilon_sq) {
					candidate.penetration = std::max(candidate.penetration, penetration);
					if (new_candidate.feature_a.value < candidate.feature_a.value ||
					    (new_candidate.feature_a == candidate.feature_a && new_candidate.feature_b.value < candidate.feature_b.value)) {
						candidate.feature_a = new_candidate.feature_a;
						candidate.feature_b = new_candidate.feature_b;
					}
					duplicate = true;
					break;
				}
			}
			if (!duplicate) {
				candidates.emplaceBack(new_candidate);
			}
		}
	}

	if (candidates.empty()) {
		auto point_a = boxSupportPoint(box_a, normal);
		auto point_b = boxSupportPoint(box_b, -normal);
		candidates.emplaceBack(
		    ContactCandidate {
		      .position = (point_a.position + point_b.position) * 0.5f,
		      .penetration = best_axis.penetration,
		      .feature_a = point_a.feature,
		      .feature_b = point_b.feature,
		    }
		);
	}

	return BoxSatContacts {.normal = normal, .candidates = candidates};
}

/// Same SAT as collideWorldBoxes but assumes box_b.rotation is identity always true for the ref voxel box
auto collideBoxAgainstAxisAlignedBox(const WorldBox& box_a, const WorldBox& box_b) -> std::optional<BoxSatContacts> {
	glm::vec3 center_delta = box_b.center - box_a.center;
	BoxSatResult best_axis;
	bool separated = false;

	auto test_axis = [&](glm::vec3 axis, BoxAxisType type, int axis_a, int axis_b, float radius_a_hint) {
		float length_squared = glm::dot(axis, axis);
		if (length_squared <= parallel_axis_epsilon_sq) {
			return;
		}

		axis /= std::sqrt(length_squared);
		float radius_a = radius_a_hint >= 0.0f ? radius_a_hint : boxProjectionRadius(box_a, axis);
		float radius_b = glm::dot(glm::abs(axis), box_b.half_extents);
		float center_distance = std::abs(glm::dot(center_delta, axis));
		float overlap = radius_a + radius_b - center_distance;
		if (overlap < -contact_tolerance) {
			separated = true;
			return;
		}

		float penetration = std::max(overlap, 0.0f);
		bool new_is_face = type != BoxAxisType::edge;
		bool old_is_face = best_axis.type != BoxAxisType::edge;
		bool better = penetration < best_axis.penetration - contact_tolerance;
		bool nearly_equal = std::abs(penetration - best_axis.penetration) <= contact_tolerance;
		if (better || (nearly_equal && new_is_face && !old_is_face)) {
			best_axis.axis = axis;
			best_axis.penetration = penetration;
			best_axis.type = type;
			best_axis.axis_a = axis_a;
			best_axis.axis_b = axis_b;
		}
	};

	for (int axis = 0; axis < 3; ++axis) {
		test_axis(box_a.rotation[axis], BoxAxisType::face_a, axis, 0, box_a.half_extents[axis]);
		if (separated) {
			return std::nullopt;
		}
	}

	static constexpr std::array<glm::vec3, 3> k_world_axes {
	  glm::vec3 {1, 0, 0},
     glm::vec3 {0, 1, 0},
     glm::vec3 {0, 0, 1}
	};
	for (int axis = 0; axis < 3; ++axis) {
		test_axis(k_world_axes[axis], BoxAxisType::face_b, 0, axis, -1.0f);
		if (separated) {
			return std::nullopt;
		}
	}

	// cross(v world basis k) by component shuffle instead of a general cross product
	auto cross_with_world_axis = [](glm::vec3 v, int k) -> glm::vec3 {
		if (k == 0) {
			return {0.0f, v.z, -v.y};
		}
		if (k == 1) {
			return {-v.z, 0.0f, v.x};
		}
		return {v.y, -v.x, 0.0f};
	};
	for (int axis_a = 0; axis_a < 3; ++axis_a) {
		for (int axis_b = 0; axis_b < 3; ++axis_b) {
			test_axis(cross_with_world_axis(box_a.rotation[axis_a], axis_b), BoxAxisType::edge, axis_a, axis_b, -1.0f);
			if (separated) {
				return std::nullopt;
			}
		}
	}

	return boxSatContactsFromAxis(box_a, box_b, best_axis, center_delta);
}

}

auto collideSpheres(BroadPhasePair pair, CollisionElement a, CollisionElement b) -> std::optional<Manifold> {
	ZoneScoped;
	ZoneValue((static_cast<uint64_t>(pair.a.shape.slot) << 32) | static_cast<uint64_t>(pair.b.shape.slot));

	const Shape& shape_a = a.shape;
	const Body& body_a = a.body;
	const Shape& shape_b = b.shape;
	const Body& body_b = b.body;

	const glm::vec3 center_a = body_a.position + body_a.rotation * shape_a.sphere.local_center;
	const glm::vec3 center_b = body_b.position + body_b.rotation * shape_b.sphere.local_center;
	const glm::vec3 delta = center_b - center_a;
	const float radius_sum = shape_a.sphere.radius + shape_b.sphere.radius;
	const float distance_squared = glm::dot(delta, delta);
	const float contact_radius = radius_sum + _detail::contact_tolerance;

	if (not std::isfinite(distance_squared) || distance_squared > contact_radius * contact_radius) {
		return std::nullopt;
	}

	const float distance = std::sqrt(distance_squared);
	glm::vec3 normal;
	if (distance_squared > _detail::direction_epsilon_sq) {
		normal = delta / distance;
	} else {
		glm::vec3 body_delta = body_b.position - body_a.position;
		float body_distance_squared = glm::dot(body_delta, body_delta);
		normal = std::isfinite(body_distance_squared) && body_distance_squared > _detail::direction_epsilon_sq
		             ? body_delta / std::sqrt(body_distance_squared)
		             : _detail::canonicalPairAxis(pair);
	}
	const glm::vec3 point_a = center_a + normal * shape_a.sphere.radius;
	const glm::vec3 point_b = center_b - normal * shape_b.sphere.radius;

	return Manifold {
	  .pair = pair,
	  .normal = normal,
	  .contacts = {ContactPoint {
	    .position = (point_a + point_b) * 0.5f,
	    .penetration = std::max(radius_sum - distance, 0.0f),
	    .feature_a = sphere_surface_feature,
	    .feature_b = sphere_surface_feature,
	    .material = _detail::combineMaterials(shape_a.material, shape_b.material),
	  }},
	  .contact_count = 1
	};
}

auto collideSphereBox(BroadPhasePair pair, CollisionElement sphere_element, CollisionElement box_element)
    -> std::optional<Manifold> {
	ZoneScoped;
	ZoneValue((static_cast<uint64_t>(pair.a.shape.slot) << 32) | static_cast<uint64_t>(pair.b.shape.slot));

	const Shape& sph_shape = sphere_element.shape;
	const Body& sph_body = sphere_element.body;
	const Shape& box_shape = box_element.shape;
	const Body& box_body = box_element.body;

	_detail::WorldSphere sphere = _detail::worldSphere(sph_body, sph_shape.sphere);
	_detail::WorldBox box = _detail::worldBox(box_body, box_shape.box);
	glm::vec3 local_sph_center = glm::transpose(box.rotation) * (sphere.center - box.center);
	glm::vec3 local_box_point = glm::clamp(local_sph_center, -box.half_extents, box.half_extents);
	glm::vec3 box_point = box.center + box.rotation * local_box_point;

	const bool center_inside = local_sph_center.x >= -box.half_extents.x && local_sph_center.x <= box.half_extents.x &&
	                           local_sph_center.y >= -box.half_extents.y && local_sph_center.y <= box.half_extents.y &&
	                           local_sph_center.z >= -box.half_extents.z && local_sph_center.z <= box.half_extents.z;

	glm::vec3 normal;
	glm::vec3 sph_point;
	glm::vec3 contact_box_point;
	float penetration;
	ContactFeatureID box_contact_feature;

	if (!center_inside) {
		glm::vec3 delta = box_point - sphere.center;
		float distance_sq = glm::dot(delta, delta);
		float contact_radius = sphere.radius + _detail::contact_tolerance;

		if (not std::isfinite(distance_sq) || distance_sq > contact_radius * contact_radius) {
			return std::nullopt;
		}

		float distance = std::sqrt(distance_sq);

		if (distance_sq > _detail::direction_epsilon_sq) {
			normal = delta / distance;
		} else {
			int selected_axis = 0;
			float greatest_excess = -1.0f;
			glm::vec3 local_normal {};

			for (int i = 0; i < 3; ++i) {
				float excess = 0.0f;
				float direction = 0.0f;

				if (local_sph_center[i] < -box.half_extents[i]) {
					excess = -box.half_extents[i] - local_sph_center[i];
					direction = 1.0f;
				} else if (local_sph_center[i] > box.half_extents[i]) {
					excess = local_sph_center[i] - box.half_extents[i];
					direction = -1.0f;
				}

				if (excess > greatest_excess) {
					greatest_excess = excess;
					selected_axis = i;
					local_normal = {};
					local_normal[i] = direction;
				}
			}

			normal = box.rotation * local_normal;
		}

		penetration = std::max(sphere.radius - distance, 0.0f);
		sph_point = sphere.center + normal * sphere.radius;
		contact_box_point = box_point;
		box_contact_feature = _detail::contactFeature(_detail::boxFeature(local_box_point, box.half_extents));
	} else {
		int selected_axis = 0;
		float nearest_face_distance = box.half_extents.x - std::abs(local_sph_center.x);

		for (int i = 1; i < 3; ++i) {
			const float face_distance = box.half_extents[i] - std::abs(local_sph_center[i]);
			if (face_distance < nearest_face_distance) {
				selected_axis = i;
				nearest_face_distance = face_distance;
			}
		}

		float face_sign = local_sph_center[selected_axis] >= 0.0f ? 1.0f : -1.0f;
		glm::vec3 local_outward {};
		local_outward[selected_axis] = face_sign;
		glm::vec3 outward = box.rotation * local_outward;
		normal = -outward;
		glm::vec3 local_face_point = local_sph_center;
		local_face_point[selected_axis] = face_sign * box.half_extents[selected_axis];
		contact_box_point = box.center + box.rotation * local_face_point;
		sph_point = sphere.center + outward * sphere.radius;
		penetration = sphere.radius + nearest_face_distance;
		box_contact_feature = boxFaceFeature(selected_axis, face_sign > 0.0f);
	}

	return Manifold {
	  .pair = pair,
	  .normal = normal,
	  .contacts = {ContactPoint {
	    .position = (sph_point + contact_box_point) * 0.5f,
	    .penetration = penetration,
	    .feature_a = sphere_surface_feature,
	    .feature_b = box_contact_feature,
	    .material = _detail::combineMaterials(sph_shape.material, box_shape.material),
	  }},
	  .contact_count = 1,
	};
}

auto collideSphereCapsule(BroadPhasePair pair, CollisionElement sphere_element, CollisionElement capsule_element)
    -> std::optional<Manifold> {
	ZoneScoped;
	ZoneValue((static_cast<uint64_t>(pair.a.shape.slot) << 32) | static_cast<uint64_t>(pair.b.shape.slot));

	const Shape& sph_shape = sphere_element.shape;
	const Body& sph_body = sphere_element.body;
	const Shape& caps_shape = capsule_element.shape;
	const Body& caps_body = capsule_element.body;

	_detail::WorldSphere sphere = _detail::worldSphere(sph_body, sph_shape.sphere);
	_detail::WorldCapsule capsule = _detail::worldCapsule(caps_body, caps_shape.capsule);
	_detail::SegmentClosestPoint closest = _detail::closestPointOnSegment(sphere.center, capsule.point_a, capsule.point_b);
	glm::vec3 delta = closest.point - sphere.center;

	// Overlap check
	float distance_squared = glm::dot(delta, delta);
	float radius_sum = sphere.radius + capsule.radius;
	float contact_radius = radius_sum + _detail::contact_tolerance;
	if (!std::isfinite(distance_squared) || distance_squared > contact_radius * contact_radius) {
		return std::nullopt;
	}

	// Penetration
	float distance = std::sqrt(distance_squared);
	glm::vec3 normal;
	if (distance_squared > _detail::direction_epsilon_sq) {
		normal = delta / distance;
	} else {
		glm::vec3 segment = capsule.point_b - capsule.point_a;
		float segment_length_squared = glm::dot(segment, segment);
		if (segment_length_squared > _detail::direction_epsilon_sq) {
			glm::vec3 segment_axis = segment / std::sqrt(segment_length_squared);
			glm::vec3 absolute_axis = glm::abs(segment_axis);
			glm::vec3 reference_axis;

			if (absolute_axis.x <= absolute_axis.y && absolute_axis.x <= absolute_axis.z) {
				reference_axis = {1.0f, 0.0f, 0.0f};
			} else if (absolute_axis.y <= absolute_axis.z) {
				reference_axis = {0.0f, 1.0f, 0.0f};
			} else {
				reference_axis = {0.0f, 0.0f, 1.0f};
			}

			normal = glm::normalize(glm::cross(segment_axis, reference_axis));
		} else {
			// The capsule behaves like a sphere when the segment has no length
			glm::vec3 body_delta = caps_body.position - sph_body.position;
			float body_distance_squared = glm::dot(body_delta, body_delta);
			if (body_distance_squared > _detail::direction_epsilon_sq) {
				normal = body_delta / std::sqrt(body_distance_squared);
			} else {
				normal = _detail::canonicalPairAxis(pair);
			}
		}
	}

	const glm::vec3 sphere_point = sphere.center + normal * sphere.radius;
	const glm::vec3 capsule_point = closest.point - normal * capsule.radius;
	const float penetration = std::max(radius_sum - distance, 0.0f);
	const glm::vec3 contact_position = (sphere_point + capsule_point) * 0.5f;

	return Manifold {
	  .pair = pair,
	  .normal = normal,
	  .contacts = {ContactPoint {
	    .position = contact_position,
	    .penetration = penetration,
	    .feature_a = sphere_surface_feature,
	    .feature_b = capsuleFeature(closest.parameter),
	    .material = _detail::combineMaterials(sph_shape.material, caps_shape.material),
	  }},
	  .contact_count = 1,
	};
}

namespace {
[[nodiscard]]
auto queryVolumeInVoxels(const AABB& bounds) -> uint64_t {
	const glm::vec3 extent = glm::max(bounds.max - bounds.min, glm::vec3(0.0f)) / voxel::k_voxel_size;
	return static_cast<uint64_t>(extent.x) * static_cast<uint64_t>(extent.y) * static_cast<uint64_t>(extent.z);
}
}

void collideSphereVoxel(
    BroadPhasePair pair, CollisionElement sphere_element, CollisionElement voxel_element, const VoxelShapeData& voxel_data,
    std::vector<Manifold>& output
) {
	ZoneScoped;
	ZoneValue((static_cast<uint64_t>(pair.a.shape.slot) << 32) | static_cast<uint64_t>(pair.b.shape.slot));

	const Shape& sph_shape = sphere_element.shape;
	const Body& sph_body = sphere_element.body;
	const Shape& voxel_shape = voxel_element.shape;
	const Body& voxel_body = voxel_element.body;

	const glm::vec3 world_center = sph_body.position + sph_body.rotation * sph_shape.sphere.local_center;
	const glm::quat voxel_rotation = glm::normalize(voxel_body.rotation * voxel_shape.voxel.local_rotation);
	const glm::vec3 voxel_origin = voxel_body.position + voxel_body.rotation * voxel_shape.voxel.local_center;
	const glm::mat3 voxel_basis = glm::mat3_cast(voxel_rotation);
	const glm::vec3 local_center = glm::transpose(voxel_basis) * (world_center - voxel_origin);

	const float radius = sph_shape.sphere.radius;
	const AABB local_bounds {
	  .min = local_center - glm::vec3(radius),
	  .max = local_center + glm::vec3(radius),
	};
	ZoneValue(queryVolumeInVoxels(local_bounds));

	const VoxelQueryContext context {
	  .volume = *voxel_data.volume, .surface = voxel_data.surface, .palette = voxel_data.palette, .materials = voxel_data.materials
	};

	struct BestContact {
		glm::vec3 local_normal;
		glm::vec3 local_point;
		glm::vec3 local_surface;
		float penetration;
		ContactFeatureID voxel_feature;
		ContactMaterial material;
	};

	// one slot per classified normal so a sphere touching a floor and a wall keeps both contacts
	std::array<std::optional<BestContact>, voxel::k_normal_direction_count> best_per_normal;

	{
		ZoneScopedN("physics::VoxelQueryWalk");
		queryVoxelSurface(context, local_bounds, [&](const VoxelCandidate& candidate) {
			if (candidate.normal_index >= best_per_normal.size()) {
				return;
			}

			const glm::vec3 closest = glm::clamp(local_center, candidate.min, candidate.max);
			const glm::vec3 delta = closest - local_center;
			const float distance_sq = glm::dot(delta, delta);
			if (not std::isfinite(distance_sq) || distance_sq > radius * radius) {
				return;
			}

			glm::vec3 local_normal;
			glm::vec3 local_surface;
			float penetration;
			if (distance_sq <= _detail::direction_epsilon_sq) {
				local_normal = -glm::vec3(voxel::normalDirection(candidate.normal_index));
				local_surface = local_center - local_normal * radius;
				penetration = radius;
			} else {
				const float distance = std::sqrt(distance_sq);
				local_normal = delta / distance;
				local_surface = local_center + local_normal * radius;
				penetration = radius - distance;
			}

			std::optional<BestContact>& slot = best_per_normal[candidate.normal_index];
			if (slot.has_value() && slot->penetration >= penetration) {
				return;
			}

			const FeatureType type =
			    candidate.classification == voxel::VoxelClass::edge ? FeatureType::voxel_edge : FeatureType::voxel_face;
			slot = BestContact {
			  .local_normal = local_normal,
			  .local_point = closest,
			  .local_surface = local_surface,
			  .penetration = penetration,
			  .voxel_feature = voxelFeature(type, candidate.brick_slot, candidate.local_index, candidate.normal_index),
			  .material = {
			               .restitution = candidate.material->restitution,
			               .static_friction = candidate.material->static_friction,
			               .dynamic_friction = candidate.material->dynamic_friction,
			               },
			};
		});
	}

	{
		ZoneScopedN("physics::BuildManifolds");
		for (size_t normal_index = 0; normal_index < best_per_normal.size(); ++normal_index) {
			const std::optional<BestContact>& best = best_per_normal[normal_index];
			if (not best.has_value()) {
				continue;
			}

			const glm::vec3 world_normal = voxel_basis * best->local_normal;
			const glm::vec3 sphere_surface_point = voxel_origin + voxel_basis * best->local_surface;
			const glm::vec3 voxel_surface_point = voxel_origin + voxel_basis * best->local_point;

			output.push_back(
			    Manifold {
			      .pair = pair,
			      .normal = world_normal,
			      .normal_index = static_cast<uint8_t>(normal_index),
			      .contacts = {ContactPoint {
			        .position = (sphere_surface_point + voxel_surface_point) * 0.5f,
			        .penetration = best->penetration,
			        .feature_a = sphere_surface_feature,
			        .feature_b = best->voxel_feature,
			        .material = best->material,
			      }},
			      .contact_count = 1,
			    }
			);
		}
	}
}

auto collideBoxes(BroadPhasePair pair, CollisionElement a, CollisionElement b) -> std::optional<Manifold> {
	ZoneScoped;
	ZoneValue((static_cast<uint64_t>(pair.a.shape.slot) << 32) | static_cast<uint64_t>(pair.b.shape.slot));

	const Shape& shape_a = a.shape;
	const Body& body_a = a.body;
	const Shape& shape_b = b.shape;
	const Body& body_b = b.body;

	_detail::WorldBox box_a = _detail::worldBox(body_a, shape_a.box);
	_detail::WorldBox box_b = _detail::worldBox(body_b, shape_b.box);

	auto sat = _detail::collideWorldBoxes(box_a, box_b);
	if (not sat.has_value()) {
		return std::nullopt;
	}

	std::vector<_detail::ContactCandidate> candidates(sat->candidates.begin(), sat->candidates.end());
	candidates = _detail::reduceContacts(std::move(candidates), sat->normal);
	const ContactMaterial material = _detail::combineMaterials(shape_a.material, shape_b.material);
	Manifold manifold {
	  .pair = pair,
	  .normal = sat->normal,
	  .contact_count = static_cast<uint8_t>(candidates.size()),
	};
	for (size_t index = 0; index < candidates.size(); ++index) {
		manifold.contacts[index] = ContactPoint {
		  .position = candidates[index].position,
		  .penetration = candidates[index].penetration,
		  .feature_a = candidates[index].feature_a,
		  .feature_b = candidates[index].feature_b,
		  .material = material,
		};
	}
	return manifold;
}

auto collideCapsuleBox(BroadPhasePair pair, CollisionElement capsule_element, CollisionElement box_element)
    -> std::optional<Manifold> {
	ZoneScoped;
	ZoneValue((static_cast<uint64_t>(pair.a.shape.slot) << 32) | static_cast<uint64_t>(pair.b.shape.slot));

	const Shape& capsule_shape = capsule_element.shape;
	const Body& capsule_body = capsule_element.body;
	const Shape& box_shape = box_element.shape;
	const Body& box_body = box_element.body;

	auto capsule = _detail::worldCapsule(capsule_body, capsule_shape.capsule);
	auto box = _detail::worldBox(box_body, box_shape.box);

	auto closest = _detail::closestPointsSegmentBox(capsule.point_a, capsule.point_b, box);
	auto delta = closest.box_point - closest.segment_point;
	float distance_squared = glm::dot(delta, delta);
	float contact_radius = capsule.radius + _detail::contact_tolerance;

	if (not std::isfinite(distance_squared) || distance_squared > contact_radius * contact_radius) {
		return std::nullopt;
	}

	float distance = std::sqrt(distance_squared);

	glm::vec3 normal;
	glm::vec3 capsule_surface;
	glm::vec3 box_surface;
	float penetration;
	ContactFeatureID capsule_contact_feature = capsuleFeature(closest.segment_parameter);
	ContactFeatureID box_contact_feature = _detail::contactFeature(closest.box_feature);

	if (distance_squared > _detail::direction_epsilon_sq) {
		// Use the direction from the capsule axis toward the box
		normal = delta / distance;
		penetration = std::max(capsule.radius - distance, 0.0f);
		capsule_surface = closest.segment_point + normal * capsule.radius;
		box_surface = closest.box_point;
	} else {
		// Move the segment into box local coordinates
		auto inverse_rotation = glm::transpose(box.rotation);
		auto local_a = inverse_rotation * (capsule.point_a - box.center);
		auto local_b = inverse_rotation * (capsule.point_b - box.center);

		// Expand the box by the capsule radius
		auto expanded_half_extents = box.half_extents + glm::vec3(capsule.radius);
		auto segment_min = glm::min(local_a, local_b);
		auto segment_max = glm::max(local_a, local_b);

		float smallest_translation = std::numeric_limits<float>::infinity();

		int selected_axis = 0;
		float selected_sign = 1.0f;

		// Check all six expanded box faces
		for (int axis = 0; axis < 3; ++axis) {
			float positive_translation = expanded_half_extents[axis] - segment_min[axis];
			if (positive_translation < smallest_translation) {
				smallest_translation = positive_translation;
				selected_axis = axis;
				selected_sign = 1.0f;
			}

			float negative_translation = segment_max[axis] + expanded_half_extents[axis];
			if (negative_translation < smallest_translation) {
				smallest_translation = negative_translation;
				selected_axis = axis;
				selected_sign = -1.0f;
			}
		}

		glm::vec3 local_outward {};
		local_outward[selected_axis] = selected_sign;

		auto outward = box.rotation * local_outward;
		normal = -outward;
		penetration = std::max(smallest_translation, 0.0f);
		auto local_segment_point = inverse_rotation * (closest.segment_point - box.center);
		auto local_face_point = glm::clamp(local_segment_point, -box.half_extents, box.half_extents);
		local_face_point[selected_axis] = selected_sign * box.half_extents[selected_axis];
		box_surface = box.center + box.rotation * local_face_point;
		capsule_surface = closest.segment_point + outward * capsule.radius;
		box_contact_feature = boxFaceFeature(selected_axis, selected_sign > 0.0f);
	}

	auto contact_position = (capsule_surface + box_surface) * 0.5f;
	return Manifold {
	  .pair = pair,
	  .normal = normal,
	  .contacts = {ContactPoint {
	    .position = contact_position,
	    .penetration = penetration,
	    .feature_a = capsule_contact_feature,
	    .feature_b = box_contact_feature,
	    .material = _detail::combineMaterials(capsule_shape.material, box_shape.material),
	  }},
	  .contact_count = 1,
	};
}

auto collideCapsules(BroadPhasePair pair, CollisionElement a, CollisionElement b) -> std::optional<Manifold> {
	ZoneScoped;
	ZoneValue((static_cast<uint64_t>(pair.a.shape.slot) << 32) | static_cast<uint64_t>(pair.b.shape.slot));

	const Shape& shape_a = a.shape;
	const Body& body_a = a.body;
	const Shape& shape_b = b.shape;
	const Body& body_b = b.body;

	auto caps_a = _detail::worldCapsule(body_a, shape_a.capsule);
	auto caps_b = _detail::worldCapsule(body_b, shape_b.capsule);

	// Find the closest points on both capsule axes
	auto closest = _detail::closestPointsBetweenSegments(caps_a.point_a, caps_a.point_b, caps_b.point_a, caps_b.point_b);
	auto delta = closest.point_b - closest.point_a;
	float distance_squared = glm::dot(delta, delta);
	float radius_sum = caps_a.radius + caps_b.radius;
	float contact_radius = radius_sum + _detail::contact_tolerance;

	if (not std::isfinite(distance_squared) || distance_squared > contact_radius * contact_radius) {
		return std::nullopt;
	}

	float distance = std::sqrt(distance_squared);
	glm::vec3 normal;

	if (distance_squared > _detail::direction_epsilon_sq) {
		normal = delta / distance;
	} else {
		auto segment_a = caps_a.point_b - caps_a.point_a;
		auto segment_b = caps_b.point_b - caps_b.point_a;
		float segment_a_length_squared = glm::dot(segment_a, segment_a);
		float segment_b_length_squared = glm::dot(segment_b, segment_b);
		auto body_delta = body_b.position - body_a.position;

		auto segment_cross = glm::cross(segment_a, segment_b);
		float cross_length_squared = glm::dot(segment_cross, segment_cross);

		// Handle intersecting capsule axes
		if (cross_length_squared > _detail::direction_epsilon_sq) {
			normal = segment_cross / std::sqrt(cross_length_squared);
		} else if (segment_a_length_squared > _detail::direction_epsilon_sq) {
			// Handle parallel or overlapping axes
			normal = _detail::perpendicularTo(segment_a);
		} else if (segment_b_length_squared > _detail::direction_epsilon_sq) {
			// Handle capsule A reduced to a sphere
			normal = _detail::perpendicularTo(segment_b);
		} else {
			// Handle both capsules reduced to spheres
			float body_distance_squared = glm::dot(body_delta, body_delta);

			if (body_distance_squared > _detail::direction_epsilon_sq) {
				normal = body_delta / std::sqrt(body_distance_squared);
			} else {
				normal = _detail::canonicalPairAxis(pair);
			}
		}

		// Point the fallback normal toward body B
		if (glm::dot(normal, body_delta) < 0.0f) {
			normal = -normal;
		}
	}

	glm::vec3 surface_a = closest.point_a + normal * caps_a.radius;
	glm::vec3 surface_b = closest.point_b - normal * caps_b.radius;
	float penetration = std::max(radius_sum - distance, 0.0f);
	auto contact_position = (surface_a + surface_b) * 0.5f;

	return Manifold {
	  .pair = pair,
	  .normal = normal,
	  .contacts = {ContactPoint {
	    .position = contact_position,
	    .penetration = penetration,
	    .feature_a = capsuleFeature(closest.parameter_a),
	    .feature_b = capsuleFeature(closest.parameter_b),
	    .material = _detail::combineMaterials(shape_a.material, shape_b.material),
	  }},
	  .contact_count = 1,
	};
}

namespace {

struct VoxelVoxelScratch {
	std::array<std::vector<_detail::ContactCandidate>, voxel::k_normal_direction_count> candidates_per_normal;
	std::array<glm::vec3, voxel::k_normal_direction_count> normal_per_group {};
	std::array<ContactMaterial, voxel::k_normal_direction_count> material_per_group {};
	std::array<bool, voxel::k_normal_direction_count> group_started {};

	void reset() {
		for (std::vector<_detail::ContactCandidate>& bucket : candidates_per_normal) {
			bucket.clear();
		}
		group_started.fill(false);
	}
};

}

void collideVoxelVoxel(
    BroadPhasePair pair, CollisionElement a, const VoxelShapeData& data_a, CollisionElement b, const VoxelShapeData& data_b,
    std::vector<Manifold>& output
) {
	ZoneScoped;
	ZoneValue((static_cast<uint64_t>(pair.a.shape.slot) << 32) | static_cast<uint64_t>(pair.b.shape.slot));

	bool a_is_probe = data_a.solid_voxel_count <= data_b.solid_voxel_count;
	auto probe = a_is_probe ? a : b;
	const VoxelShapeData& probe_data = a_is_probe ? data_a : data_b;
	auto ref = a_is_probe ? b : a;
	const VoxelShapeData& ref_data = a_is_probe ? data_b : data_a;

	glm::quat probe_rotation = glm::normalize(probe.body.rotation * probe.shape.voxel.local_rotation);
	glm::vec3 probe_origin = probe.body.position + probe.body.rotation * probe.shape.voxel.local_center;
	glm::mat3 probe_basis = glm::mat3_cast(probe_rotation);
	glm::mat3 probe_basis_inv = glm::transpose(probe_basis);

	glm::quat ref_rotation = glm::normalize(ref.body.rotation * ref.shape.voxel.local_rotation);
	glm::vec3 ref_origin = ref.body.position + ref.body.rotation * ref.shape.voxel.local_center;
	glm::mat3 ref_basis = glm::mat3_cast(ref_rotation);
	glm::mat3 ref_basis_inv = glm::transpose(ref_basis);

	// clang-format off
	VoxelQueryContext probe_context = {
		.volume = *probe_data.volume,
		.surface = probe_data.surface,
		.palette = probe_data.palette,
		.materials = probe_data.materials,
	};
	
	VoxelQueryContext ref_context = {
		.volume = *ref_data.volume,
		.surface = ref_data.surface,
		.palette = ref_data.palette,
		.materials = ref_data.materials,
	};
	// clang-format on

	const glm::mat3 reference_to_probe = probe_basis_inv * ref_basis;
	const glm::vec3 reference_to_probe_offset = probe_basis_inv * (ref_origin - probe_origin);
	const AABB& reference_bounds = ref.shape.voxel.local_bounds;
	const glm::vec3 reference_center = (reference_bounds.min + reference_bounds.max) * 0.5f;
	const glm::vec3 reference_extent = (reference_bounds.max - reference_bounds.min) * 0.5f;
	const glm::vec3 reference_center_in_probe = (reference_to_probe * reference_center) + reference_to_probe_offset;
	const glm::vec3 reference_extent_in_probe = glm::abs(reference_to_probe[0]) * reference_extent.x +
	                                            glm::abs(reference_to_probe[1]) * reference_extent.y +
	                                            glm::abs(reference_to_probe[2]) * reference_extent.z;

	const AABB probe_search_bounds {
	  .min = glm::max(probe.shape.voxel.local_bounds.min, reference_center_in_probe - reference_extent_in_probe),
	  .max = glm::min(probe.shape.voxel.local_bounds.max, reference_center_in_probe + reference_extent_in_probe),
	};
	if (glm::any(glm::greaterThan(probe_search_bounds.min, probe_search_bounds.max))) {
		return;
	}
	ZoneValue(queryVolumeInVoxels(probe_search_bounds));
	ZoneValue(queryVolumeInVoxels(probe.shape.voxel.local_bounds));
	ZoneValue(queryVolumeInVoxels(ref.shape.voxel.local_bounds));

	static thread_local VoxelVoxelScratch scratch;
	scratch.reset();
	auto& candidates_per_normal = scratch.candidates_per_normal;
	auto& normal_per_group = scratch.normal_per_group;
	auto& material_per_group = scratch.material_per_group;
	auto& group_started = scratch.group_started;
	size_t remaining_budget = 256;

	{
		ZoneScopedN("physics::VoxelQueryWalk");
		queryVoxelSurface(probe_context, probe_search_bounds, [&](const VoxelCandidate& probe_c) -> bool {
			if (remaining_budget == 0) {
				return false;
			}

			glm::vec3 probe_center_world = probe_origin + probe_basis * ((probe_c.min + probe_c.max) * 0.5f);
			glm::vec3 half_extent = (probe_c.max - probe_c.min) * 0.5f;

			// clang-format off
		_detail::WorldBox probe_box_reference_local {
		  .center = ref_basis_inv * (probe_center_world - ref_origin),
		  .rotation = ref_basis_inv * probe_basis,
		  .half_extents = half_extent,
		};
			// clang-format on

			glm::vec3 extents = glm::abs(probe_box_reference_local.rotation[0]) * half_extent.x +
			                    glm::abs(probe_box_reference_local.rotation[1]) * half_extent.y +
			                    glm::abs(probe_box_reference_local.rotation[2]) * half_extent.z;
			AABB probe_bounds_reference_local {
			  .min = probe_box_reference_local.center - extents,
			  .max = probe_box_reference_local.center + extents,
			};

			FeatureType probe_f_type;
			{
				using voxel::VoxelClass;
				probe_f_type = probe_c.classification == VoxelClass::edge ? FeatureType::voxel_edge : FeatureType::voxel_face;
			}
			ContactFeatureID probe_feature = voxelFeature(probe_f_type, probe_c.brick_slot, probe_c.local_index, probe_c.normal_index);
			queryVoxelSurface(ref_context, probe_bounds_reference_local, [&](const VoxelCandidate& ref_c) -> bool {
				if (remaining_budget == 0) {
					return false;
				}
				if (ref_c.normal_index >= candidates_per_normal.size()) {
					return true;
				}
				--remaining_budget;

				_detail::WorldBox ref_voxel_box {
				  .center = (ref_c.min + ref_c.max) * 0.5f,
				  .rotation = glm::mat3(1.0f),
				  .half_extents = (ref_c.max - ref_c.min) * 0.5f,
				};
				// ref_voxel_box is always axis aligned so this skips the generic cross products
				auto sat = _detail::collideBoxAgainstAxisAlignedBox(probe_box_reference_local, ref_voxel_box);
				if (not sat.has_value()) {
					return true;
				}

				auto ref_f_type = ref_c.classification == voxel::VoxelClass::edge ? FeatureType::voxel_edge : FeatureType::voxel_face;
				auto ref_feature = voxelFeature(ref_f_type, ref_c.brick_slot, ref_c.local_index, ref_c.normal_index);

				std::vector<_detail::ContactCandidate>& bucket = candidates_per_normal[ref_c.normal_index];
				for (_detail::ContactCandidate& raw : sat->candidates) {
					raw.feature_a = probe_feature;
					raw.feature_b = ref_feature;
					bucket.push_back(raw);
				}

				// clang-format off
			if (not group_started[ref_c.normal_index]) {
				group_started[ref_c.normal_index] = true;
				normal_per_group[ref_c.normal_index] = sat->normal;
				material_per_group[ref_c.normal_index] = _detail::combineMaterials(
					PhysicsMaterial {
						.restitution = probe_c.material->restitution,
						.static_friction = probe_c.material->static_friction,
						.dynamic_friction = probe_c.material->dynamic_friction
					},
					PhysicsMaterial {
						.restitution = ref_c.material->restitution,
						.static_friction = ref_c.material->static_friction,
						.dynamic_friction = ref_c.material->dynamic_friction
					}
				);
			}
				// clang-format on
				return true;
			});

			return remaining_budget > 0;
		});
	}

	{
		ZoneScopedN("physics::BuildManifolds");
		for (size_t i = 0; i < candidates_per_normal.size(); ++i) {
			std::vector<_detail::ContactCandidate>& bucket = candidates_per_normal[i];
			if (bucket.empty()) {
				continue;
			}

			glm::vec3 local_normal = normal_per_group[i];
			bucket = _detail::reduceContacts(std::move(bucket), local_normal);
			const std::vector<_detail::ContactCandidate>& reduced = bucket;
			glm::vec3 world_normal = ref_basis * local_normal;

			Manifold manifold {
			  .pair = pair,
			  .normal = a_is_probe ? world_normal : -world_normal,
			  .normal_index = static_cast<uint8_t>(i),
			  .contact_count = static_cast<uint8_t>(reduced.size()),
			};

			for (size_t j = 0; j < reduced.size(); ++j) {
				manifold.contacts[j] = ContactPoint {
				  .position = ref_origin + ref_basis * reduced[j].position,
				  .penetration = reduced[j].penetration,
				  .feature_a = a_is_probe ? reduced[j].feature_a : reduced[j].feature_b,
				  .feature_b = a_is_probe ? reduced[j].feature_b : reduced[j].feature_a,
				  .material = material_per_group[i],
				};
			}

			output.push_back(manifold);
		}
	}
}

void collideCapsuleVoxel(
    BroadPhasePair pair, CollisionElement capsule_element, CollisionElement voxel_element, const VoxelShapeData& voxel_data,
    std::vector<Manifold>& output
) {
	ZoneScoped;
	ZoneValue((static_cast<uint64_t>(pair.a.shape.slot) << 32) | static_cast<uint64_t>(pair.b.shape.slot));

	const Shape& caps_shape = capsule_element.shape;
	const Body& caps_body = capsule_element.body;
	const Shape& voxel_shape = voxel_element.shape;
	const Body& voxel_body = voxel_element.body;

	const _detail::WorldCapsule capsule = _detail::worldCapsule(caps_body, caps_shape.capsule);

	const glm::quat voxel_rotation = glm::normalize(voxel_body.rotation * voxel_shape.voxel.local_rotation);
	const glm::vec3 voxel_origin = voxel_body.position + voxel_body.rotation * voxel_shape.voxel.local_center;
	const glm::mat3 voxel_basis = glm::mat3_cast(voxel_rotation);
	const glm::mat3 voxel_basis_inv = glm::transpose(voxel_basis);

	const glm::vec3 local_a = voxel_basis_inv * (capsule.point_a - voxel_origin);
	const glm::vec3 local_b = voxel_basis_inv * (capsule.point_b - voxel_origin);
	const float radius = capsule.radius;

	const AABB local_bounds {
	  .min = glm::min(local_a, local_b) - glm::vec3(radius),
	  .max = glm::max(local_a, local_b) + glm::vec3(radius),
	};
	ZoneValue(queryVolumeInVoxels(local_bounds));
	const VoxelQueryContext context {
	  .volume = *voxel_data.volume, .surface = voxel_data.surface, .palette = voxel_data.palette, .materials = voxel_data.materials
	};

	struct BestContact {
		glm::vec3 local_normal;
		glm::vec3 local_point;
		glm::vec3 local_surface;
		float penetration;
		float segment_parameter;
		ContactFeatureID voxel_feature;
		ContactMaterial material;
	};

	// one slot per classified normal, same reasoning as collideSphereVoxel
	std::array<std::optional<BestContact>, voxel::k_normal_direction_count> best_per_normal;

	{
		ZoneScopedN("physics::VoxelQueryWalk");
		queryVoxelSurface(context, local_bounds, [&](const VoxelCandidate& candidate) {
			if (candidate.normal_index >= best_per_normal.size()) {
				return;
			}

			const _detail::WorldBox voxel_box {
			  .center = (candidate.min + candidate.max) * 0.5f,
			  .rotation = glm::mat3(1.0f),
			  .half_extents = (candidate.max - candidate.min) * 0.5f,
			};

			const _detail::SegmentBoxClosestPoints closest = _detail::closestPointsSegmentBox(local_a, local_b, voxel_box);
			const glm::vec3 delta = closest.box_point - closest.segment_point;
			const float distance_sq = glm::dot(delta, delta);
			if (not std::isfinite(distance_sq) || distance_sq > radius * radius) {
				return;
			}

			glm::vec3 local_normal;
			glm::vec3 local_surface;
			float penetration;
			if (distance_sq <= _detail::direction_epsilon_sq) {
				local_normal = -glm::vec3(voxel::normalDirection(candidate.normal_index));
				local_surface = closest.segment_point - local_normal * radius;
				penetration = radius;
			} else {
				const float distance = std::sqrt(distance_sq);
				local_normal = delta / distance;
				local_surface = closest.segment_point + local_normal * radius;
				penetration = radius - distance;
			}

			std::optional<BestContact>& slot = best_per_normal[candidate.normal_index];
			if (slot.has_value() && slot->penetration >= penetration) {
				return;
			}

			const FeatureType type =
			    candidate.classification == voxel::VoxelClass::edge ? FeatureType::voxel_edge : FeatureType::voxel_face;
			slot = BestContact {
			  .local_normal = local_normal,
			  .local_point = closest.box_point,
			  .local_surface = local_surface,
			  .penetration = penetration,
			  .segment_parameter = closest.segment_parameter,
			  .voxel_feature = voxelFeature(type, candidate.brick_slot, candidate.local_index, candidate.normal_index),
			  .material = {
			               .restitution = candidate.material->restitution,
			               .static_friction = candidate.material->static_friction,
			               .dynamic_friction = candidate.material->dynamic_friction,
			               },
			};
		});
	}

	{
		ZoneScopedN("physics::BuildManifolds");
		for (size_t normal_index = 0; normal_index < best_per_normal.size(); ++normal_index) {
			const std::optional<BestContact>& best = best_per_normal[normal_index];
			if (not best.has_value()) {
				continue;
			}

			const glm::vec3 world_normal = voxel_basis * best->local_normal;
			const glm::vec3 capsule_surface_point = voxel_origin + voxel_basis * best->local_surface;
			const glm::vec3 voxel_surface_point = voxel_origin + voxel_basis * best->local_point;

			output.push_back(
			    Manifold {
			      .pair = pair,
			      .normal = world_normal,
			      .normal_index = static_cast<uint8_t>(normal_index),
			      .contacts = {ContactPoint {
			        .position = (capsule_surface_point + voxel_surface_point) * 0.5f,
			        .penetration = best->penetration,
			        .feature_a = capsuleFeature(best->segment_parameter),
			        .feature_b = best->voxel_feature,
			        .material = best->material,
			      }},
			      .contact_count = 1,
			    }
			);
		}
	}
}

void collideBoxVoxel(
    BroadPhasePair pair, CollisionElement box_element, CollisionElement voxel_element, const VoxelShapeData& voxel_data,
    std::vector<Manifold>& output
) {
	ZoneScoped;
	ZoneValue((static_cast<uint64_t>(pair.a.shape.slot) << 32) | static_cast<uint64_t>(pair.b.shape.slot));

	const Shape& box_shape = box_element.shape;
	const Body& box_body = box_element.body;
	const Shape& voxel_shape = voxel_element.shape;
	const Body& voxel_body = voxel_element.body;

	const _detail::WorldBox world_box = _detail::worldBox(box_body, box_shape.box);

	const glm::quat voxel_rotation = glm::normalize(voxel_body.rotation * voxel_shape.voxel.local_rotation);
	const glm::vec3 voxel_origin = voxel_body.position + voxel_body.rotation * voxel_shape.voxel.local_center;
	const glm::mat3 voxel_basis = glm::mat3_cast(voxel_rotation);
	const glm::mat3 voxel_basis_inv = glm::transpose(voxel_basis);

	const _detail::WorldBox local_box {
	  .center = voxel_basis_inv * (world_box.center - voxel_origin),
	  .rotation = voxel_basis_inv * world_box.rotation,
	  .half_extents = world_box.half_extents,
	};

	const glm::vec3 local_world_extents = glm::abs(local_box.rotation[0]) * local_box.half_extents.x +
	                                      glm::abs(local_box.rotation[1]) * local_box.half_extents.y +
	                                      glm::abs(local_box.rotation[2]) * local_box.half_extents.z;
	const AABB local_bounds {
	  .min = local_box.center - local_world_extents,
	  .max = local_box.center + local_world_extents,
	};
	ZoneValue(queryVolumeInVoxels(local_bounds));
	ZoneValue(queryVolumeInVoxels(voxel_shape.voxel.local_bounds));
	const VoxelQueryContext context {
	  .volume = *voxel_data.volume, .surface = voxel_data.surface, .palette = voxel_data.palette, .materials = voxel_data.materials
	};

	std::array<std::vector<_detail::ContactCandidate>, voxel::k_normal_direction_count> candidates_per_normal;
	std::array<glm::vec3, voxel::k_normal_direction_count> normal_per_group {};
	std::array<ContactMaterial, voxel::k_normal_direction_count> material_per_group {};
	std::array<bool, voxel::k_normal_direction_count> group_started {};

	{
		ZoneScopedN("physics::VoxelQueryWalk");
		queryVoxelSurface(context, local_bounds, [&](const VoxelCandidate& candidate) {
			if (candidate.normal_index >= candidates_per_normal.size()) {
				return;
			}

			const _detail::WorldBox voxel_box {
			  .center = (candidate.min + candidate.max) * 0.5f,
			  .rotation = glm::mat3(1.0f),
			  .half_extents = (candidate.max - candidate.min) * 0.5f,
			};

			ZoneScopedN("physics::BoxVoxelSAT");
			auto sat = _detail::collideWorldBoxes(local_box, voxel_box);
			if (not sat.has_value()) {
				return;
			}

			const FeatureType type =
			    candidate.classification == voxel::VoxelClass::edge ? FeatureType::voxel_edge : FeatureType::voxel_face;
			const ContactFeatureID voxel_feature =
			    voxelFeature(type, candidate.brick_slot, candidate.local_index, candidate.normal_index);

			std::vector<_detail::ContactCandidate>& bucket = candidates_per_normal[candidate.normal_index];
			for (_detail::ContactCandidate& raw : sat->candidates) {
				raw.feature_b = voxel_feature;
				bucket.push_back(raw);
			}

			if (not group_started[candidate.normal_index]) {
				group_started[candidate.normal_index] = true;
				normal_per_group[candidate.normal_index] = sat->normal;
				material_per_group[candidate.normal_index] = {
				  .restitution = candidate.material->restitution,
				  .static_friction = candidate.material->static_friction,
				  .dynamic_friction = candidate.material->dynamic_friction,
				};
			}
		});
	}

	{
		ZoneScopedN("physics::BuildManifolds");
		for (size_t normal_index = 0; normal_index < candidates_per_normal.size(); ++normal_index) {
			std::vector<_detail::ContactCandidate>& bucket = candidates_per_normal[normal_index];
			if (bucket.empty()) {
				continue;
			}

			const glm::vec3 local_normal = normal_per_group[normal_index];
			std::vector<_detail::ContactCandidate> reduced = _detail::reduceContacts(std::move(bucket), local_normal);
			const glm::vec3 world_normal = voxel_basis * local_normal;

			Manifold manifold {
			  .pair = pair,
			  .normal = world_normal,
			  .normal_index = static_cast<uint8_t>(normal_index),
			  .contact_count = static_cast<uint8_t>(reduced.size()),
			};
			for (size_t index = 0; index < reduced.size(); ++index) {
				manifold.contacts[index] = ContactPoint {
				  .position = voxel_origin + voxel_basis * reduced[index].position,
				  .penetration = reduced[index].penetration,
				  .feature_a = reduced[index].feature_a,
				  .feature_b = reduced[index].feature_b,
				  .material = material_per_group[normal_index],
				};
			}
			output.push_back(manifold);
		}
	}
}

}
