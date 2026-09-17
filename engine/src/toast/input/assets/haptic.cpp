#include "haptic.hpp"

#include <algorithm>
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

}

Haptic::Haptic(const toml::table& table, Handle<Schema> schema) : Data(table, std::move(schema), Data::keep_all_keys) {
	const auto& d = static_cast<const DataValue&>(m_root);

	std::string mode_str;
	if (d.contains("mode")) {
		mode_str = d["mode"].as<std::string>();
	}
	m_mode = hapticModeFromString(mode_str);

	if (d.contains("priority")) {
		m_priority = static_cast<int>(d["priority"].as<int64_t>());
	}
	if (d.contains("duration_ms")) {
		m_duration_ms = static_cast<uint32_t>(d["duration_ms"].as<int64_t>());
	}

	switch (m_mode) {
		case HapticMode::standard:
			if (d.contains("left")) {
				m_left = static_cast<float>(d["left"].as<double>());
			}
			if (d.contains("right")) {
				m_right = static_cast<float>(d["right"].as<double>());
			}
			break;

		case HapticMode::curve: {
			std::string channels_str;
			if (d.contains("channels")) {
				channels_str = d["channels"].as<std::string>();
			}
			m_channels = (channels_str == "dual") ? HapticChannels::dual : HapticChannels::single;

			if (d.contains("pan")) {
				m_pan = std::clamp(static_cast<float>(d["pan"].as<double>()), -1.0f, 1.0f);
			}
			if (d.contains("multiplier")) {
				m_multiplier = static_cast<float>(d["multiplier"].as<double>());
			}

			if (d.contains("curve") && d["curve"].isObject()) {
				try {
					m_curve = Curve::fromToml(d["curve"].asTomlTable());
				} catch (const std::exception& e) { TOAST_WARN("Haptic", "Failed to parse [curve]: {}", e.what()); }
			}
			if (!m_curve) {
				TOAST_WARN("Haptic", "curve mode is missing a valid [curve] table");
			}

			if (m_channels == HapticChannels::dual) {
				if (d.contains("curve_right") && d["curve_right"].isObject()) {
					try {
						m_curve_right = Curve::fromToml(d["curve_right"].asTomlTable());
					} catch (const std::exception& e) { TOAST_WARN("Haptic", "Failed to parse [curve_right]: {}", e.what()); }
				}
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

auto Haptic::sample(float t01) const -> glm::vec2 {
	const float t = std::clamp(t01, 0.0f, 1.0f);

	switch (m_mode) {
		case HapticMode::standard: return {std::clamp(m_left, 0.0f, 1.0f), std::clamp(m_right, 0.0f, 1.0f)};

		case HapticMode::curve: {
			if (!m_curve) {
				return {0.0f, 0.0f};
			}

			const float left_y = m_curve->evalAtX(t) * m_multiplier;

			if (m_channels == HapticChannels::dual) {
				const float right_y = m_curve_right ? m_curve_right->evalAtX(t) * m_multiplier : 0.0f;
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
