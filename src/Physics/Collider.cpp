#include "Delaunay.hpp"
#include "Engine/Renderer/DebugDrawLayer.hpp"

#include <Engine/Physics/Collider.hpp>
#include <Engine/Toast/Objects/Actor.hpp>
#include <imgui.h>
#include <unordered_set>

using namespace physics;

void Collider::Inspector() {
	if (ImGui::Button("Add Point")) {
		AddPoint();
	}
}

static void DebugDrawTrianglesUniqueEdges(const std::vector<glm::dvec2>& pts, const std::vector<Triangle>& tris) {
	auto edge_key = [](int i, int j) {
		if (i > j) {
			std::swap(i, j);
		}
		return std::to_string(i) + ":" + std::to_string(j);
	};

	std::unordered_set<std::string> drawn;
	auto draw_edge = [&](int i, int j) {
		std::string key = edge_key(i, j);
		if (drawn.insert(key).second) {
			renderer::DebugLine(pts[i], pts[j], { 1.0f, 0.0f, 0.0f, 1.0f });
		}
	};

	for (const auto& t : tris) {
		draw_edge(t.a, t.b);
		draw_edge(t.b, t.c);
		draw_edge(t.c, t.a);
	}
}

void Collider::EditorTick() {
	std::vector<glm::dvec2> points;
	points.reserve(children.size());

	int i = 0;
	for (const auto& [_, child] : children) {
		if (strcmp(child->type(), "Actor")) {
			continue;
		}
		auto position = static_cast<toast::Actor*>(child.get())->transform()->worldPosition();
		points.emplace_back(position.x, position.y);
	}

	if (points.size() < 3) {
		return;
	}
	auto triangles = delaunayTriangulate(points);
	DebugDrawTrianglesUniqueEdges(points, triangles);
}

void Collider::AddPoint() {
	m.pointCount++;
	children.Add<toast::Actor>("Point " + std::to_string(m.pointCount));
}
