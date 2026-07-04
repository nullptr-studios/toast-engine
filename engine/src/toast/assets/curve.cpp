#include "curve.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <toast/log.hpp>

namespace assets {

namespace {

auto splineTypeToString(SplineType t) -> std::string_view {
	switch (t) {
		case SplineType::linear: return "linear";
		case SplineType::catmull_rom: return "catmull_rom";
		case SplineType::bspline: return "bspline";
		case SplineType::bezier: return "bezier";
	}
	return "linear";
}

auto splineTypeFromString(std::string_view s) -> SplineType {
	if (s == "catmull_rom") {
		return SplineType::catmull_rom;
	}
	if (s == "bspline") {
		return SplineType::bspline;
	}
	if (s == "bezier") {
		return SplineType::bezier;
	}
	return SplineType::linear;
}

auto dimToString(CurveDimension d) -> std::string_view {
	return d == CurveDimension::d2 ? "2d" : "3d";
}

}

Curve::Curve(std::vector<float> points, CurveDimension dim, SplineType type, float t_scale)
    : m_points(std::move(points)),
      m_dim(dim),
      m_spline_type(type),
      m_t_scale(t_scale) {
	rebuildSpline();
}

auto Curve::fromToml(const toml::table& tbl) -> std::unique_ptr<Curve> {
	auto spline_type_str = tbl["spline_type"].value<std::string>().value_or("linear");
	auto dimension_str = tbl["dimension"].value<std::string>().value_or("2d");
	auto t_scale = (float)tbl["t_scale"].value<double>().value_or(1.0);

	auto dim = (dimension_str == "3d") ? CurveDimension::d3 : CurveDimension::d2;
	auto type = splineTypeFromString(spline_type_str);
	size_t n_components = (dim == CurveDimension::d2) ? 2 : 3;

	const auto* points_arr = tbl["points"].as_array();
	if (!points_arr) {
		throw std::runtime_error("Curve: missing 'points' array");
	}

	std::vector<float> points;
	points.reserve(points_arr->size() * n_components);

	for (const auto& entry : *points_arr) {
		const auto* pt = entry.as_table();
		if (!pt) {
			throw std::runtime_error("Curve: points entry is not a table");
		}

		const auto* x = pt->get("x");
		const auto* y = pt->get("y");
		if (!x || !y) {
			throw std::runtime_error("Curve: point missing x or y");
		}

		points.push_back((float)x->value<double>().value_or(0.0));
		points.push_back((float)y->value<double>().value_or(0.0));

		if (dim == CurveDimension::d3) {
			const auto* z = pt->get("z");
			points.push_back(z ? (float)z->value<double>().value_or(0.0) : 0.0f);
		}
	}

	return std::make_unique<Curve>(std::move(points), dim, type, t_scale);
}

auto Curve::serialize(SaveMode /*mode*/) const -> std::vector<uint8_t> {
	std::ostringstream ss;
	ss << "spline_type = \"" << splineTypeToString(m_spline_type) << "\"\n";
	ss << "dimension   = \"" << dimToString(m_dim) << "\"\n";
	ss << "t_scale     = " << m_t_scale << "\n";

	size_t dim = dimCount();
	size_t n = m_points.size() / dim;

	for (size_t i = 0; i < n; ++i) {
		ss << "\n[[points]]\n";
		ss << "x = " << m_points[(i * dim) + 0] << "\n";
		ss << "y = " << m_points[(i * dim) + 1] << "\n";
		if (dim == 3) {
			ss << "z = " << m_points[(i * dim) + 2] << "\n";
		}
	}

	auto str = ss.str();
	return {str.begin(), str.end()};
}

auto Curve::eval2D(float t) const -> glm::vec2 {
	double u = std::clamp((double)(t / m_t_scale), 0.0, 1.0);
	auto v = m_spline.eval(u).resultVec2();
	return {(float)v.x(), (float)v.y()};
}

auto Curve::eval3D(float t) const -> glm::vec3 {
	double u = std::clamp((double)(t / m_t_scale), 0.0, 1.0);
	auto v = m_spline.eval(u).resultVec3();
	return {(float)v.x(), (float)v.y(), (float)v.z()};
}

void Curve::setPoints(std::vector<float> points) {
	m_points = std::move(points);
	rebuildSpline();
}

void Curve::rebuildSpline() {
	size_t dim = dimCount();
	size_t n = m_points.size() / dim;

	if (n < 2) {
		TOAST_WARN("Curve", "Need at least 2 control points; got {}", n);
		return;
	}

	std::vector<double> pts(m_points.begin(), m_points.end());

	try {
		switch (m_spline_type) {
			case SplineType::linear:
				m_spline = tinyspline::BSpline(n, dim, 1, tinyspline::BSpline::Type::Clamped);
				m_spline.setControlPoints(pts);
				break;

			case SplineType::bspline: {
				size_t degree = std::min<size_t>(3, n - 1);
				m_spline = tinyspline::BSpline(n, dim, degree, tinyspline::BSpline::Type::Clamped);
				m_spline.setControlPoints(pts);
				break;
			}

			case SplineType::bezier: {
				if ((n - 1) % 3 != 0 || n < 4) {
					TOAST_WARN("Curve", "Bezier requires 3k+1 control points; got {}. Falling back to bspline.", n);
					size_t degree = std::min<size_t>(3, n - 1);
					m_spline = tinyspline::BSpline(n, dim, degree, tinyspline::BSpline::Type::Clamped);
					m_spline.setControlPoints(pts);
				} else {
					size_t segments = (n - 1) / 3;
					std::vector<double> expanded;
					expanded.reserve(segments * 4 * dim);
					for (size_t s = 0; s < segments; ++s) {
						for (size_t p = 0; p <= 3; ++p) {
							for (size_t d = 0; d < dim; ++d) {
								expanded.push_back(pts[(((s * 3) + p) * dim) + d]);
							}
						}
					}
					m_spline = tinyspline::BSpline(segments * 4, dim, 3, tinyspline::BSpline::Type::Beziers);
					m_spline.setControlPoints(expanded);
				}
				break;
			}

			case SplineType::catmull_rom: m_spline = tinyspline::BSpline::interpolateCatmullRom(pts, dim); break;
		}
	} catch (const std::exception& e) { TOAST_ERROR("Curve", "Failed to build spline: {}", e.what()); }
}

}
