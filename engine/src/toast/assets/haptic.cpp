#include "haptic.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>
#include <toast/log.hpp>

namespace assets {

namespace {

auto hapticModeFromString(std::string_view s) -> HapticMode {
	if (s == "curve") {
		return HapticMode::curve;
	}
	if (s == "adaptive_trigger") {
		return HapticMode::adaptive_trigger;
	}
	if (s == "audio_haptic") {
		return HapticMode::audio_haptic;
	}
	return HapticMode::standard;
}

auto curveFromSubTable(const toml::table& parent, std::string_view key) -> std::unique_ptr<Curve> {
	const auto* sub = parent[key].as_table();
	if (sub == nullptr) {
		return nullptr;
	}
	try {
		return Curve::fromToml(*sub);
	} catch (const std::exception& e) {
		TOAST_ERROR("Haptic", "Failed to parse [{}] curve: {}", key, e.what());
		return nullptr;
	}
}

}

Haptic::Haptic(toml::table table) : m_table(std::move(table)) {
	m_mode = hapticModeFromString(m_table["mode"].value<std::string>().value_or("standard"));
	m_priority = static_cast<int>(m_table["priority"].value<int64_t>().value_or(0));
	m_duration_ms = static_cast<uint32_t>(m_table["duration_ms"].value<int64_t>().value_or(0));

	switch (m_mode) {
		case HapticMode::standard:
			m_left = static_cast<float>(m_table["left"].value<double>().value_or(0.0));
			m_right = static_cast<float>(m_table["right"].value<double>().value_or(0.0));
			break;

		case HapticMode::curve: {
			const bool dual = m_table["channels"].value<std::string>().value_or("single") == "dual";
			m_channels = dual ? HapticChannels::dual : HapticChannels::single;
			m_pan = std::clamp(static_cast<float>(m_table["pan"].value<double>().value_or(0.0)), -1.0f, 1.0f);
			m_multiplier = static_cast<float>(m_table["multiplier"].value<double>().value_or(1.0));

			m_curve = curveFromSubTable(m_table, "curve");
			if (!m_curve) {
				TOAST_WARN("Haptic", "curve mode is missing a valid [curve] table");
			}
			if (m_channels == HapticChannels::dual) {
				m_curve_right = curveFromSubTable(m_table, "curve_right");
				if (!m_curve_right) {
					TOAST_WARN("Haptic", "dual channels need a valid [curve_right] table");
				}
			}
			break;
		}

		case HapticMode::adaptive_trigger:
		case HapticMode::audio_haptic: break;
	}
}

auto Haptic::serialize(SaveMode /*mode*/) const -> std::vector<uint8_t> {
	std::ostringstream ss;
	ss << m_table;
	auto str = ss.str();
	return {str.begin(), str.end()};
}

auto Haptic::sample(float t01) const -> glm::vec2 {
	const float t = std::clamp(t01, 0.0f, 1.0f);

	switch (m_mode) {
		case HapticMode::standard: return {std::clamp(m_left, 0.0f, 1.0f), std::clamp(m_right, 0.0f, 1.0f)};

		case HapticMode::curve: {
			if (!m_curve) {
				return {0.0f, 0.0f};
			}

			const float left_y = m_curve->eval2D(t * m_curve->tScale()).y * m_multiplier;

			if (m_channels == HapticChannels::dual) {
				const float right_y = m_curve_right ? m_curve_right->eval2D(t * m_curve_right->tScale()).y * m_multiplier : 0.0f;
				return {std::clamp(left_y, 0.0f, 1.0f), std::clamp(right_y, 0.0f, 1.0f)};
			}

			// -1 left .. +1 right
			const float left_gain = m_pan > 0.0f ? 1.0f - m_pan : 1.0f;
			const float right_gain = m_pan < 0.0f ? 1.0f + m_pan : 1.0f;
			return {std::clamp(left_y * left_gain, 0.0f, 1.0f), std::clamp(left_y * right_gain, 0.0f, 1.0f)};
		}

		case HapticMode::adaptive_trigger:
		case HapticMode::audio_haptic: return {0.0f, 0.0f};
	}

	return {0.0f, 0.0f};
}

}
