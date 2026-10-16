#include "curve.hpp"

#include <algorithm>
#include <array>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>

namespace assets {

namespace {

constexpr std::array<std::string_view, 3> k_channel_names {"x", "y", "z"};

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
	switch (d) {
		case CurveDimension::d2: return "2d";
		case CurveDimension::d3: return "3d";
		default: return "1d";
	}
}

struct ParsedCurve {
	std::vector<Curve::Points> channels;
	SplineType type = SplineType::linear;
	float t_scale = 1.0f;
};

auto number(const toml::node* node) -> float {
	return node != nullptr ? static_cast<float>(node->value<double>().value_or(0.0)) : 0.0f;
}

auto parsePoints(const toml::array& points) -> Curve::Points {
	Curve::Points parsed;
	parsed.reserve(points.size() * 2);
	for (const auto& entry : points) {
		const auto* point = entry.as_table();
		if (point == nullptr) {
			throw std::runtime_error("Curve: points entry is not a table");
		}
		if (point->get("x") == nullptr || point->get("y") == nullptr) {
			throw std::runtime_error("Curve: point missing x or y");
		}
		parsed.push_back(number(point->get("x")));
		parsed.push_back(number(point->get("y")));
	}
	return parsed;
}

auto legacyPath(const toml::array& points) -> std::vector<Curve::Points> {
	std::vector<Curve::Points> channels(3);
	const size_t count = points.size();
	for (size_t i = 0; i < count; ++i) {
		const auto* point = points[i].as_table();
		if (point == nullptr) {
			throw std::runtime_error("Curve: points entry is not a table");
		}
		const float t = count > 1 ? static_cast<float>(i) / static_cast<float>(count - 1) : 0.0f;
		for (size_t axis = 0; axis < 3; ++axis) {
			channels[axis].push_back(t);
			channels[axis].push_back(number(point->get(k_channel_names[axis])));
		}
	}
	TOAST_WARN("Curve", "Converted an old 3D path curve to x, y and z channels; save it to keep them");
	return channels;
}

auto normalizeX(Curve::Points& points) -> float {
	if (points.size() < 4) {
		return 1.0f;
	}
	const float first = points.front();
	const float range = points[points.size() - 2] - first;
	if (range <= 1.0e-6f) {
		return 1.0f;
	}
	float previous = 0.0f;
	for (size_t i = 0; i + 1 < points.size(); i += 2) {
		points[i] = previous = std::clamp((points[i] - first) / range, previous, 1.0f);
	}
	points[points.size() - 2] = 1.0f;
	return range;
}

auto parseCurve(const toml::table& tbl) -> ParsedCurve {
	ParsedCurve parsed;
	parsed.type = splineTypeFromString(tbl["spline_type"].value<std::string>().value_or("linear"));
	parsed.t_scale = static_cast<float>(tbl["t_scale"].value<double>().value_or(1.0));
	const auto dimension = tbl["dimension"].value<std::string>().value_or("1d");

	if (const auto* channels = tbl["channels"].as_array()) {
		for (const auto& entry : *channels) {
			const auto* channel = entry.as_table();
			const auto* points = channel != nullptr ? channel->get_as<toml::array>("points") : nullptr;
			if (points == nullptr) {
				throw std::runtime_error("Curve: channel without a 'points' array");
			}
			Curve::Points keys = parsePoints(*points);
			normalizeX(keys);
			parsed.channels.push_back(std::move(keys));
		}
		if (parsed.channels.empty() || parsed.channels.size() > 3) {
			throw std::runtime_error("Curve: needs 1 to 3 channels");
		}
		return parsed;
	}

	const auto* points = tbl["points"].as_array();
	if (points == nullptr) {
		throw std::runtime_error("Curve: missing 'points' array");
	}
	if (dimension == "3d") {
		parsed.channels = legacyPath(*points);
	} else {
		Curve::Points keys = parsePoints(*points);
		parsed.t_scale *= normalizeX(keys);
		parsed.channels.push_back(std::move(keys));
	}
	return parsed;
}

}

Curve::Curve(std::vector<Points> channels, SplineType type, float t_scale) : m_spline_type(type), m_t_scale(t_scale) {
	m_channels.reserve(channels.size());
	for (Points& points : channels) {
		Channel& channel = m_channels.emplace_back(Channel {.points = std::move(points), .spline = {}});
		rebuildSpline(channel);
	}
}

auto Curve::fromToml(const toml::table& tbl) -> std::unique_ptr<Curve> {
	ZoneScoped;
	ParsedCurve parsed = parseCurve(tbl);
	return std::make_unique<Curve>(std::move(parsed.channels), parsed.type, parsed.t_scale);
}

void Curve::reload(const toml::table& tbl) {
	ZoneScoped;
	ParsedCurve parsed = parseCurve(tbl);
	for (const Points& points : parsed.channels) {
		if (points.size() / 2 < 2) {
			throw std::runtime_error("Curve: needs at least 2 points per channel");
		}
	}

	m_spline_type = parsed.type;
	m_t_scale = parsed.t_scale;
	m_channels.clear();
	for (Points& points : parsed.channels) {
		Channel& channel = m_channels.emplace_back(Channel {.points = std::move(points), .spline = {}});
		rebuildSpline(channel);
	}
}

auto Curve::serialize(SaveMode /*mode*/) const -> std::vector<uint8_t> {
	ZoneScoped;
	std::ostringstream ss;
	ss << "spline_type = \"" << splineTypeToString(m_spline_type) << "\"\n";
	ss << "dimension   = \"" << dimToString(dimension()) << "\"\n";
	ss << "t_scale     = " << m_t_scale << "\n";

	const auto write_points = [&ss](const Points& points, std::string_view table) {
		for (size_t i = 0; i + 1 < points.size(); i += 2) {
			ss << "\n[[" << table << "]]\n";
			ss << "x = " << points[i] << "\n";
			ss << "y = " << points[i + 1] << "\n";
		}
	};

	if (dimension() == CurveDimension::d1) {
		write_points(m_channels.front().points, "points");
	} else {
		for (size_t i = 0; i < m_channels.size(); ++i) {
			ss << "\n[[channels]]\nname = \"" << k_channel_names[i] << "\"\n";
			write_points(m_channels[i].points, "channels.points");
		}
	}

	auto str = ss.str();
	return {str.begin(), str.end()};
}

auto Curve::evalChannel(size_t channel_index, float t) const -> float {
	return sampleChannel(channel_index, t / std::max(m_t_scale, 1.0e-4f));
}

auto Curve::sampleChannel(size_t channel_index, float x) const -> float {
	if (channel_index >= m_channels.size()) {
		return 0.0f;
	}
	const Channel& channel = m_channels[channel_index];
	if (channel.points.size() < 4) {
		return channel.points.size() >= 2 ? channel.points[1] : 0.0f;
	}
	try {
		return static_cast<float>(channel.spline.bisect(static_cast<double>(std::clamp(x, 0.0f, 1.0f))).resultVec2().y());
	} catch (const std::exception& e) {
		TOAST_WARN("Curve", "bisect failed at x = {}: {}; falling back to parametric eval", x, e.what());
		const double u = std::clamp(static_cast<double>(x), 0.0, 1.0);
		return static_cast<float>(channel.spline.eval(u).resultVec2().y());
	}
}

auto Curve::eval1D(float t) const -> float {
	return evalChannel(0, t);
}

auto Curve::eval2D(float t) const -> glm::vec2 {
	if (dimension() == CurveDimension::d1) {
		return glm::vec2(eval1D(t));
	}
	return {evalChannel(0, t), evalChannel(1, t)};
}

auto Curve::eval3D(float t) const -> glm::vec3 {
	if (dimension() == CurveDimension::d1) {
		return glm::vec3(eval1D(t));
	}
	return {evalChannel(0, t), evalChannel(1, t), evalChannel(2, t)};
}

auto Curve::xRange() const noexcept -> glm::vec2 {
	if (m_channels.empty() || m_channels.front().points.size() < 2) {
		return glm::vec2 {0.0f};
	}
	const Points& points = m_channels.front().points;
	return {points.front(), points[points.size() - 2]};
}

void Curve::setPoints(size_t channel, Points points) {
	if (channel >= m_channels.size()) {
		return;
	}
	m_channels[channel].points = std::move(points);
	rebuildSpline(m_channels[channel]);
}

void Curve::rebuildSpline(Channel& channel) const {
	ZoneScoped;
	constexpr size_t dim = 2;
	const size_t n = channel.points.size() / dim;

	if (n < 2) {
		TOAST_WARN("Curve", "Need at least 2 control points; got {}", n);
		return;
	}

	const std::vector<double> pts(channel.points.begin(), channel.points.end());
	tinyspline::BSpline& spline = channel.spline;

	try {
		switch (m_spline_type) {
			case SplineType::linear:
				spline = tinyspline::BSpline(n, dim, 1, tinyspline::BSpline::Type::Clamped);
				spline.setControlPoints(pts);
				break;

			case SplineType::bspline: {
				size_t degree = std::min<size_t>(3, n - 1);
				spline = tinyspline::BSpline(n, dim, degree, tinyspline::BSpline::Type::Clamped);
				spline.setControlPoints(pts);
				break;
			}

			case SplineType::bezier: {
				if ((n - 1) % 3 != 0 || n < 4) {
					TOAST_WARN("Curve", "Bezier requires 3k+1 control points; got {}. Falling back to bspline.", n);
					size_t degree = std::min<size_t>(3, n - 1);
					spline = tinyspline::BSpline(n, dim, degree, tinyspline::BSpline::Type::Clamped);
					spline.setControlPoints(pts);
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
					spline = tinyspline::BSpline(segments * 4, dim, 3, tinyspline::BSpline::Type::Beziers);
					spline.setControlPoints(expanded);
				}
				break;
			}

			case SplineType::catmull_rom: spline = tinyspline::BSpline::interpolateCatmullRom(pts, dim); break;
		}
	} catch (const std::exception& e) { TOAST_ERROR("Curve", "Failed to build spline: {}", e.what()); }
}

}
