/**
 * @file trackpad_math.hpp
 * @author Xein
 * @date 16 Sep 2026
 * @brief Trackpad
 */

#pragma once

#include <cmath>

namespace toast::input {

struct TrackpadTransform {
	float scale;
	float x;
	float y;
};

struct TrackpadDelta {
	float dx;
	float dy;
	float zoom;
};

class TrackpadGesture {
public:
	static constexpr float pinch_scale = 125.0f;
	static constexpr float epsilon = 3e-5f;

	void reset() noexcept {
		m_kind = Kind::none;
		m_last = {pinch_scale, 0.0f, 0.0f};
	}

	[[nodiscard]]
	auto started() const noexcept -> bool {
		return m_kind != Kind::none;
	}

	auto accept(TrackpadTransform current) noexcept -> TrackpadDelta {
		if (current.scale == 0.0f || (std::abs(current.scale - m_last.scale) <= epsilon &&
		                              std::abs(current.x - m_last.x) <= epsilon && std::abs(current.y - m_last.y) <= epsilon)) {
			return {0.0f, 0.0f, 0.0f};
		}

		if (m_kind == Kind::none) {
			m_kind = Kind::pan;
		}
		if (m_kind == Kind::pan && std::abs(current.scale - pinch_scale) > epsilon) {
			m_kind = Kind::pinch;
		}

		if (m_kind == Kind::pinch) {
			const float zoom = current.scale - m_last.scale;
			m_last.scale = current.scale;
			return {0.0f, 0.0f, zoom};
		}

		const TrackpadDelta delta {current.x - m_last.x, current.y - m_last.y, 0.0f};
		m_last.x = current.x;
		m_last.y = current.y;
		return delta;
	}

private:
	enum class Kind : uint8_t {
		none,
		pan,
		pinch
	};

	Kind m_kind = Kind::none;
	TrackpadTransform m_last {pinch_scale, 0.0f, 0.0f};
};

}
