/**
 * @file haptic.hpp
 * @author Xein
 * @date 22 Jun 2026
 *
 * @brief Asset describing a controller haptic effect loaded from a .thaptic file
 */

#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <toast/assets/curve.hpp>
#include <toast/assets/data.hpp>

namespace assets {

enum class HapticMode : uint8_t {
	standard,
	curve,
	adaptive_trigger,    // TODO: implement this
	audio_haptic,        // TODO: implement this
};

enum class HapticChannels : uint8_t {
	single,
	dual,
};

/**
 * @brief Asset representing a single haptic effect loaded from a .thaptic file
 */
class TOAST_API Haptic : public Data {
public:
	explicit Haptic(const toml::table& table, Handle<Schema> schema = {});

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "haptic";
	}

	[[nodiscard]]
	auto mode() const noexcept -> HapticMode {
		return m_mode;
	}

	[[nodiscard]]
	auto priority() const noexcept -> int {
		return m_priority;
	}

	[[nodiscard]]
	auto durationMs() const noexcept -> uint32_t {
		return m_duration_ms;
	}

	/**
	 * @brief Samples the effect's motor intensities at a normalized playback time
	 * @param t Normalized playback time in [0, 1]
	 * @returns {left, right} motor intensities in [0, 1]
	 */
	[[nodiscard]]
	auto sample(float t) const -> glm::vec2;

private:
	HapticMode m_mode = HapticMode::standard;
	int m_priority = 0;
	uint32_t m_duration_ms = 0;

	// standard mode
	float m_left = 0.0f;
	float m_right = 0.0f;

	// curve mode
	HapticChannels m_channels = HapticChannels::single;
	float m_pan = 0.0f;
	float m_multiplier = 1.0f;
	std::unique_ptr<Curve> m_curve;          ///< single curve, or left motor when dual
	std::unique_ptr<Curve> m_curve_right;    ///< right motor when dual
};

}
