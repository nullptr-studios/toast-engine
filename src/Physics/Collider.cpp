#include "Toast/Physics/Collider.hpp"

#include "ConvexCollider.hpp"
#include "Toast/GlmJson.hpp"
#include "Toast/Objects/Actor.hpp"
#include "Toast/Renderer/DebugDrawLayer.hpp"

#ifdef TOAST_EDITOR
#include <imgui.h>
#endif

using namespace physics;

void Collider::AddPoint(glm::vec2 point) {
	m.points.emplace_back(point);
}

void Collider::SwapPoints(glm::vec2 lhs, glm::vec2 rhs) {
	auto lhs_it = std::ranges::find(m.points, lhs);
	auto rhs_it = std::ranges::find(m.points, rhs);
	std::swap(lhs_it, rhs_it);
}

void Collider::DeletePoint(glm::vec2 point) {
	auto it = std::ranges::find(m.points, point);
	m.points.erase(it);
}

void Collider::CalculatePoints() {
	// We require at leats 3 points to make a mesh
	if (m.points.size() < 3) {
		return;
	}

	// update world position data
	glm::vec2 world_position = static_cast<toast::Actor*>(parent())->transform()->worldPosition();
	data.worldPosition = world_position;

	for (auto* c : m.convexShapes) {
		delete c;
	}
	m.convexShapes.clear();

	// simple_meshes are a list of points marked true if concave
	using simple_mesh = std::list<std::pair<glm::vec2, bool>>;
	simple_mesh start_mesh;

	// Figure out which points are concave
	int sign = ShoelaceArea(m.points) >= 0 ? 1 : -1;
	std::list<std::list<glm::vec2>::iterator> concave_points;
	for (auto it = m.points.begin(); it != m.points.end(); ++it) {
		auto it_prev = it != m.points.begin() ? std::prev(it) : std::prev(m.points.end());
		auto it_next = std::next(it) != m.points.end() ? std::next(it) : m.points.begin();

		glm::vec2 curr = *it;
		glm::vec2 prev = *it_prev;
		glm::vec2 next = *it_next;

		glm::mat2 mat = { curr - prev, next - curr };
		float det = sign * glm::determinant(mat);

		// we mark the point as concave if the determinant is negative
		std::pair<glm::vec2, bool> point = { curr, det < 0.0f };
		start_mesh.push_back(point);
	}

	// Create convex meshes
	std::list<simple_mesh> meshes_list = { start_mesh };

	// for every mesh, if we find a point marked as concave, we will divide the
	// mesh into two by creating a new point from the intersection of the direction of
	// the vector created by normalize((concave_pt - prev) + (concave_pt - next)) and the
	// line of the mesh it intersects with, if it exactly happened to be another point
	// rather than a line we will pick that point instead of creating a new one at the
	// intersection point
	// This will mark that point as not concave since its not concave anymore
	//
	// We will run this recursivelly until there are no points marked as concave

	// Recompute concavity flags for a mesh
	auto recompute_concave = [&](simple_mesh& mesh) {
		for (auto it = mesh.begin(); it != mesh.end(); ++it) {
			auto it_prev = it != mesh.begin() ? std::prev(it) : std::prev(mesh.end());
			auto it_next = std::next(it) != mesh.end() ? std::next(it) : mesh.begin();

			glm::vec2 curr = it->first;
			glm::vec2 prev = it_prev->first;
			glm::vec2 next = it_next->first;

			glm::mat2 mat = { curr - prev, next - curr };
			float det = sign * glm::determinant(mat);
			it->second = det < 0.0f;    // concave flag
		}
	};

	const float epsilon = 1e-5f;
	bool changed = true;
	while (changed) {
		changed = false;

		// Iterate over each mesh we currently have
		for (auto mesh_it = meshes_list.begin(); mesh_it != meshes_list.end();) {
			auto& mesh = *mesh_it;

			// Find the first concave point in this mesh
			auto concave_it = std::ranges::find_if(mesh, [](const auto& p) {
				return p.second;
			});

			// If no concave points, skip to the next mesh
			if (concave_it == mesh.end()) {
				++mesh_it;
				continue;
			}

			// Find concave point and its neighbors
			glm::vec2 concave_pt = concave_it->first;
			auto prev_it = concave_it != mesh.begin() ? std::prev(concave_it) : std::prev(mesh.end());
			auto next_it = std::next(concave_it) != mesh.end() ? std::next(concave_it) : mesh.begin();

			// Direction is the bisector of the two vectors
			glm::vec2 dir = (concave_pt - prev_it->first) + (concave_pt - next_it->first);
			float dir_len = glm::length(dir);
			if (dir_len > epsilon) {
				dir = glm::normalize(dir);
			} else {
				// perpendicular direction to prev->concave if thedirection doesn't make sense
				glm::vec2 edge = concave_pt - prev_it->first;
				float edge_len = glm::length(edge);
				if (edge_len > epsilon) {
					dir = glm::normalize(glm::vec2(-edge.y, edge.x));
				} else {
					dir = glm::vec2(1.0f, 0.0f);    // safe fallback
				}
			}

			// Track the closest intersection along direction
			float best_t = std::numeric_limits<float>::max();
			glm::vec2 best_point = concave_pt;
			auto best_edge_start = mesh.end();

			// Test intersection with every edge that does NOT include the concave point
			for (auto it = mesh.begin(); it != mesh.end(); ++it) {
				auto it_next = std::next(it) != mesh.end() ? std::next(it) : mesh.begin();

				// Skip edges incident to the concave point
				if (it == concave_it || it_next == concave_it) {
					continue;
				}

				glm::vec2 q = it->first;             // edge start
				glm::vec2 s = it_next->first - q;    // edge direction

				float denom = glm::determinant(glm::mat2 { dir, s });
				if (std::abs(denom) < epsilon) {
					continue;    // Parallel, no intersection
				}

				// Solve for intersection concave_pt + t*dir = q + u*s
				float t = glm::determinant(glm::mat2 { q - concave_pt, s }) / denom;
				float u = glm::determinant(glm::mat2 { q - concave_pt, dir }) / denom;

				// Accept only forward intersections and within the edge length
				if (t > epsilon && u >= -epsilon && u <= 1.0f + epsilon) {
					if (t < best_t) {
						best_t = t;
						best_point = concave_pt + dir * t;
						best_edge_start = it;
					}
				}
			}

			// If no intersection found, mark concave resolved and continue
			if (best_edge_start == mesh.end()) {
				concave_it->second = false;
				++mesh_it;
				continue;
			}

			auto best_edge_end = std::next(best_edge_start) != mesh.end() ? std::next(best_edge_start) : mesh.begin();

			// If the intersection is very close to an existing vertex, snap to it
			if (glm::length(best_point - best_edge_start->first) < epsilon) {
				best_point = best_edge_start->first;
			} else if (glm::length(best_point - best_edge_end->first) < epsilon) {
				best_point = best_edge_end->first;
			}

			// This new point will be shared by the two new meshes and is not concave
			std::pair<glm::vec2, bool> new_point = { best_point, false };

			// Build two new meshes by walking the old mesh in two directions from the concave point
			simple_mesh mesh_a;
			simple_mesh mesh_b;

			// mesh_a: from concave -> ... -> best_edge_start -> new_point
			for (auto it = concave_it;; it = (std::next(it) != mesh.end() ? std::next(it) : mesh.begin())) {
				mesh_a.push_back(*it);
				if (it == best_edge_start) {
					break;
				}
			}
			mesh_a.push_back(new_point);

			// mesh_b: new_point -> best_edge_end -> ... -> concave
			mesh_b.push_back(new_point);
			for (auto it = best_edge_end;; it = (std::next(it) != mesh.end() ? std::next(it) : mesh.begin())) {
				mesh_b.push_back(*it);
				if (it == concave_it) {
					break;
				}
			}

			// Re-evaluate concavity after splitting
			recompute_concave(mesh_a);
			recompute_concave(mesh_b);

			// Replace the current mesh with the two new meshes
			mesh_it = meshes_list.erase(mesh_it);
			mesh_it = meshes_list.insert(mesh_it, mesh_b);
			mesh_it = meshes_list.insert(mesh_it, mesh_a);

			changed = true;
			break;    // restart scanning meshes after modification
		}
	}

	data.flags = m.flags;
	data.parent = parent();

	// Finally, create convex colliders for every convex mesh we produced
	for (const auto& points : meshes_list) {
		m.convexShapes.emplace_back(new ConvexCollider(points, data));
	}
}

void Collider::Destroy() {
	for (auto* c : m.convexShapes) {
		delete c;
	}
}

#ifdef TOAST_EDITOR
#pragma region EDITOR

void Collider::Inspector() {
	ImGui::Spacing();
	ImGui::SeparatorText("Properties");

	const double min = 0.0;
	const double max = 1.5;
	ImGui::SliderScalar("Friction", ImGuiDataType_Double, &data.friction, &min, &max);

	ImGui::Spacing();
	ImGui::SeparatorText("Collider Flags");
	unsigned int cur = static_cast<unsigned int>(m.flags);
	bool default_flag = (cur & static_cast<unsigned int>(ColliderFlags::Default)) != 0;
	bool ground_flag = (cur & static_cast<unsigned int>(ColliderFlags::Ground)) != 0;
	bool player_flag = (cur & static_cast<unsigned int>(ColliderFlags::Player)) != 0;
	bool enemy_flag = (cur & static_cast<unsigned int>(ColliderFlags::Enemy)) != 0;

	if (ImGui::Checkbox("Default", &default_flag)) {
		if (default_flag) {
			cur |= static_cast<unsigned int>(ColliderFlags::Default);
		} else {
			cur &= ~static_cast<unsigned int>(ColliderFlags::Default);
		}
	}
	if (ImGui::Checkbox("Ground", &ground_flag)) {
		if (ground_flag) {
			cur |= static_cast<unsigned int>(ColliderFlags::Ground);
		} else {
			cur &= ~static_cast<unsigned int>(ColliderFlags::Ground);
		}
	}
	if (ImGui::Checkbox("Enemy", &enemy_flag)) {
		if (enemy_flag) {
			cur |= static_cast<unsigned int>(ColliderFlags::Enemy);
		} else {
			cur &= ~static_cast<unsigned int>(ColliderFlags::Enemy);
		}
	}
	if (ImGui::Checkbox("Player", &player_flag)) {
		if (player_flag) {
			cur |= static_cast<unsigned int>(ColliderFlags::Player);
		} else {
			cur &= ~static_cast<unsigned int>(ColliderFlags::Player);
		}
	}

	m.flags = static_cast<ColliderFlags>(cur);
	data.flags = static_cast<ColliderFlags>(cur);
	for (auto* c : m.convexShapes) {
		c->flags = static_cast<ColliderFlags>(cur);
	}

	ImGui::Spacing();
	ImGui::SeparatorText("Points");

	if (ImGui::Button("Add")) {
		AddPoint(debug.newPointPosition);
	}
	ImGui::SameLine();
	ImGui::DragFloat2("Position", &debug.newPointPosition.x);

	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Indent(10);

	int idx = 0;
	for (auto it = m.points.begin(); it != m.points.end();) {
		ImGui::PushID(idx);

		bool moved = false;

		// Up: only if not first
		if (it != m.points.begin() && ImGui::SmallButton("U")) {
			auto prev_it = std::prev(it);
			std::iter_swap(it, prev_it);
			it = prev_it;
			moved = true;
		}
		ImGui::SameLine();

		// Down: only if not last
		auto next_it = std::next(it);
		if (next_it != m.points.end() && ImGui::SmallButton("D")) {
			std::iter_swap(it, next_it);
			it = next_it;
			moved = true;
		}
		ImGui::SameLine();

		ImGui::Text("Point %d", idx + 1);
		ImGui::SameLine();

		if (ImGui::SmallButton("X")) {
			DeletePoint(*it);
			ImGui::PopID();
			ImGui::Separator();
			ImGui::Spacing();
			return;
		}

		std::string label = std::format("Position##pt{}", idx);
		ImGui::DragFloat2(label.c_str(), &it->x);

		ImGui::PopID();
		ImGui::Separator();
		ImGui::Spacing();

		++it;
		++idx;
	}

	ImGui::Unindent(10);

	if (ImGui::Button("Calculate mesh")) {
		CalculatePoints();
	}

	ImGui::Spacing();
	ImGui::SeparatorText("Debug");

	ImGui::Checkbox("Show points", &debug.showPoints);
	ImGui::Checkbox("Show colliders", &debug.showColliders);
	ImGui::Checkbox("Show normals", &data.debugNormals);
}

void Collider::EditorTick() {
	glm::vec2 world_position = static_cast<toast::Actor*>(parent())->transform()->worldPosition();

	if (debug.showPoints) {
		renderer::DebugCircle(debug.newPointPosition + world_position, 0.1f, { 1.0f, 0.5f, 0.0f, 1.0f });

		for (glm::vec2 p : m.points) {
			renderer::DebugCircle(p + world_position, 0.1f);
		}
	}

	if (debug.showColliders) {
		for (auto& c : m.convexShapes) {
			c->Debug();
		}
	}
}

#pragma endregion
#endif

json_t Collider::Save() const {
	json_t j = Component::Save();

	for (const auto& p : m.points) {
		j["points"].push_back(p);
	}

	j["friction"] = data.friction;

	j["debug.showPoints"] = debug.showPoints;
	j["debug.showColliders"] = debug.showColliders;
	j["debug.showNormals"] = data.debugNormals;
	j["flags"] = static_cast<unsigned int>(m.flags);

	return j;
}

void Collider::Load(json_t j, bool propagate) {
	if (j.contains("points")) {
		// we need to clear the points before loading so we don't have duplicates
		if (!m.points.empty()) {
			m.points.clear();
		}

		for (const auto& p : j["points"]) {
			AddPoint(p);
		}

		CalculatePoints();
	}

	if (j.contains("friction")) {
		data.friction = j["friction"];
	}

	if (j.contains("debug.showPoints")) {
		debug.showPoints = j["debug.showPoints"];
	}
	if (j.contains("debug.showColliders")) {
		debug.showColliders = j["debug.showColliders"];
	}
	if (j.contains("debug.showNormals")) {
		data.debugNormals = j["debug.showNormals"];
	}
	if (j.contains("flags")) {
		m.flags = static_cast<ColliderFlags>(j["flags"].get<unsigned int>());
	}

	Component::Load(j, propagate);
}
