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
	d1 = 1,
	d2 = 2,
	d3 = 3,
};

class TOAST_API Curve : public Asset, public ISaveable {
public:
	using Points = std::vector<float>;

	Curve(std::vector<Points> channels, SplineType type, float t_scale = 1.0f);

	[[nodiscard]]
	static auto fromToml(const toml::table& tbl) -> std::unique_ptr<Curve>;

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "curve";
	}

	[[nodiscard]]
	auto serialize(SaveMode mode) const -> std::vector<uint8_t> override;

	[[nodiscard]]
	auto eval1D(float t) const -> float;

	[[nodiscard]]
	auto eval2D(float t) const -> glm::vec2;

	[[nodiscard]]
	auto eval3D(float t) const -> glm::vec3;

	[[nodiscard]]
	auto evalChannel(size_t channel, float t) const -> float;

	[[nodiscard]]
	auto evalAtX(float x) const -> float {
		return sampleChannel(0, x);
	}

	[[nodiscard]]
	auto xRange() const noexcept -> glm::vec2;

	[[nodiscard]]
	auto dimension() const noexcept -> CurveDimension {
		return static_cast<CurveDimension>(m_channels.size());
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
	auto numPoints(size_t channel = 0) const noexcept -> size_t {
		return channel < m_channels.size() ? m_channels[channel].points.size() / 2 : 0;
	}

	void setPoints(size_t channel, Points points);

	/// Throws and keeps the old curve when the table is not a curve, nothing may sample it while this runs
	void reload(const toml::table& tbl);

private:
	struct Channel {
		Points points;
		tinyspline::BSpline spline;
	};

	/// @p x is in the 0 to 1 the keys live in
	[[nodiscard]]
	auto sampleChannel(size_t channel, float x) const -> float;

	void rebuildSpline(Channel& channel) const;

	std::vector<Channel> m_channels;
	SplineType m_spline_type;
	float m_t_scale = 1.0f;
};

}
