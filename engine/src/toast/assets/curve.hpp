/**
 * @file curve.hpp
 * @author Xein
 * @date 21 Jun 2026
 * @brief Spline curve asset using tinyspline
 */

#pragma once
#include "core_types.hpp"

#include <glm/glm.hpp>
#include <tinysplinecxx.h>

namespace assets {

enum class SplineType : uint8_t {
	linear,         ///< linear
	catmull_rom,    ///< automatic tangents
	bspline,        ///< approximates central points
	bezier,         ///< bezier
};

enum class CurveDimension : uint8_t {
	d2,
	d3,
};

class TOAST_API Curve : public Asset, public ISaveable {
public:
	Curve(std::vector<float> points, CurveDimension dim, SplineType type, float t_scale = 1.0f);

	[[nodiscard]]
	static auto fromToml(const toml::table& tbl) -> std::unique_ptr<Curve>;

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "curve";
	}

	[[nodiscard]]
	auto serialize(SaveMode mode) const -> std::vector<uint8_t> override;

	[[nodiscard]]
	auto eval2D(float t) const -> glm::vec2;

	[[nodiscard]]
	auto eval3D(float t) const -> glm::vec3;

	[[nodiscard]]
	auto dimension() const noexcept -> CurveDimension {
		return m_dim;
	}

	[[nodiscard]]
	auto splineType() const noexcept -> SplineType {
		return m_spline_type;
	}

	[[nodiscard]]
	auto tScale() const noexcept -> float {
		return m_t_scale;
	}

	[[nodiscard]]
	auto numPoints() const noexcept -> size_t {
		return m_points.size() / dimCount();
	}

	void setPoints(std::vector<float> points);

private:
	[[nodiscard]]
	auto dimCount() const noexcept -> size_t {
		return m_dim == CurveDimension::d2 ? 2 : 3;
	}

	void rebuildSpline();

	std::vector<float> m_points;
	CurveDimension m_dim;
	SplineType m_spline_type;
	float m_t_scale = 1.0f;
	tinyspline::BSpline m_spline;
};

}
