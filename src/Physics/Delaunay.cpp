#include "Delaunay.hpp"

#include <algorithm>
#include <glm/glm.hpp>
#include <limits>

namespace physics {

namespace {
struct Circumcircle {
	glm::dvec2 center;
	double r2;
};

// Compute circumcircle of triangle (pa, pb, pc)
Circumcircle circumcircle(const glm::dvec2& pa, const glm::dvec2& pb, const glm::dvec2& pc) {
	// From barycentric formula
	double d = 2.0 * (pa.x * (pb.y - pc.y) + pb.x * (pc.y - pa.y) + pc.x * (pa.y - pb.y));
	// Fallback to large circle if degenerate
	if (std::abs(d) < 1e-18) {
		return { (pa + pb + pc) / 3.0, std::numeric_limits<double>::infinity() };
	}
	double pa2 = glm::dot(pa, pa);
	double pb2 = glm::dot(pb, pb);
	double pc2 = glm::dot(pc, pc);

	double ux = (pa2 * (pb.y - pc.y) + pb2 * (pc.y - pa.y) + pc2 * (pa.y - pb.y)) / d;
	double uy = (pa2 * (pc.x - pb.x) + pb2 * (pa.x - pc.x) + pc2 * (pc.x - pa.x)) / d;

	glm::dvec2 c { ux, uy };
	return { c, glm::dot(pa - c, pa - c) };
}

bool pointInCircumcircle(const glm::dvec2& p, const glm::dvec2& a, const glm::dvec2& b, const glm::dvec2& c, double eps) {
	auto cc = circumcircle(a, b, c);
	double d2 = glm::dot(p - cc.center, p - cc.center);
	return d2 <= cc.r2 + eps;
}
}

std::vector<Triangle> delaunayTriangulate(const std::vector<glm::dvec2>& pts, double eps) {
	std::vector<glm::dvec2> points = pts;
	std::vector<Triangle> triangles;

	if (points.size() < 3) {
		return triangles;
	}

	// Build supertriangle
	double min_x = points[0].x, max_x = points[0].x;
	double min_y = points[0].y, max_y = points[0].y;
	for (auto& p : points) {
		min_x = std::min(min_x, p.x);
		max_x = std::max(max_x, p.x);
		min_y = std::min(min_y, p.y);
		max_y = std::max(max_y, p.y);
	}
	double dx = max_x - min_x;
	double dy = max_y - min_y;
	double delta_max = std::max(dx, dy);
	double midx = (min_x + max_x) * 0.5;
	double midy = (min_y + max_y) * 0.5;

	// Big triangle covering all points
	glm::dvec2 p1 { midx - (2 * delta_max), midy - delta_max };
	glm::dvec2 p2 { midx, midy + (2 * delta_max) };
	glm::dvec2 p3 { midx + (2 * delta_max), midy - delta_max };

	int super_a = static_cast<int>(points.size());
	int super_b = super_a + 1;
	int super_c = super_a + 2;

	points.push_back(p1);
	points.push_back(p2);
	points.push_back(p3);

	triangles.push_back({ super_a, super_b, super_c });

	// Insert points
	for (int pi = 0; pi < (int)pts.size(); ++pi) {
		const auto& p = points[pi];
		std::vector<int> bad;
		for (int ti = 0; ti < (int)triangles.size(); ++ti) {
			const auto& t = triangles[ti];
			if (pointInCircumcircle(p, points[t.a], points[t.b], points[t.c], eps)) {
				bad.push_back(ti);
			}
		}

		// Collect boundary edges
		std::vector<Edge> edges;
		auto add_edge = [&](int a, int b) {
			Edge e { a, b };
			auto it = std::find(edges.begin(), edges.end(), e);
			if (it != edges.end()) {
				edges.erase(it);    // shared edge; remove
			} else {
				edges.push_back(e);
			}
		};

		for (int idx : bad) {
			const auto& t = triangles[idx];
			add_edge(t.a, t.b);
			add_edge(t.b, t.c);
			add_edge(t.c, t.a);
		}

		// Remove bad triangles
		// remove from back to keep indices valid
		std::sort(bad.begin(), bad.end());
		for (int k = (int)bad.size() - 1; k >= 0; --k) {
			triangles.erase(triangles.begin() + bad[k]);
		}

		// Re-triangulate the hole
		for (auto& e : edges) {
			triangles.push_back({ e.a, e.b, pi });
		}
	}

	// Remove triangles touching supertriangle vertices
	triangles.erase(
	    std::remove_if(
	        triangles.begin(),
	        triangles.end(),
	        [&](const Triangle& t) {
		        return t.a >= super_a || t.b >= super_a || t.c >= super_a;
	        }
	    ),
	    triangles.end()
	);

	return triangles;
}

}

