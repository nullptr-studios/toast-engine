#include "ruler.hpp"

#include "camera.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_access.hpp>
#include <toast/renderer/debug/debug_draw.hpp>
#include <toast/voxel/voxel_constants.hpp>

namespace toast {

namespace {

const glm::vec4 k_color {1.0f, 0.62f, 0.12f, 1.0f};
const glm::vec4 k_preview_color {1.0f, 0.85f, 0.45f, 0.9f};
constexpr float k_leg_alpha = 0.55f;
constexpr float k_min_length = 1e-4f;
constexpr float k_max_ticks = 40.0f;
constexpr float k_marker_pixels = 4.0f;
constexpr float k_tick_pixels = 5.0f;
constexpr float k_label_epsilon = 0.25f;

/// World metres one pixel spans at @p point
auto pixelSize(const Camera& camera, glm::vec3 point, glm::vec2 viewport) -> float {
	const float distance = glm::distance(camera.world_position, point);
	return 2.0f * distance * std::tan(glm::radians(camera.fov) * 0.5f) / std::max(viewport.y, 1.0f);
}

/// Steps through 0.1 0.5 1 5 10 until a segment fits in k_max_ticks
auto tickStep(float length) -> float {
	float step = voxel::k_voxel_size;
	bool by_five = true;
	while (length / step > k_max_ticks) {
		step *= by_five ? 5.0f : 2.0f;
		by_five = !by_five;
	}
	return step;
}

auto project(const Camera& camera, glm::vec2 viewport, glm::vec3 point) -> std::optional<glm::vec2> {
	const glm::vec4 clip = camera.getProjection(viewport.x / viewport.y) * camera.getView() * glm::vec4(point, 1.0f);
	if (clip.w <= 0.0f) {
		return std::nullopt;
	}
	const glm::vec2 ndc = glm::vec2(clip) / clip.w;
	return glm::vec2((ndc.x + 1.0f) * 0.5f * viewport.x, (ndc.y + 1.0f) * 0.5f * viewport.y);
}

auto labelOf(const Camera& camera, glm::vec2 viewport, glm::vec3 a, glm::vec3 b) -> event::RulerSegment {
	event::RulerSegment out {.a = a, .b = b};
	if (const auto screen = project(camera, viewport, (a + b) * 0.5f)) {
		out.label = *screen;
		out.label_visible =
		    screen->x > -64.0f && screen->y > -64.0f && screen->x < viewport.x + 64.0f && screen->y < viewport.y + 64.0f;
	}
	return out;
}

void drawMarker(const Camera& camera, glm::vec2 viewport, glm::vec3 point, glm::vec4 color) {
	debug::drawSphere(point, k_marker_pixels * pixelSize(camera, point, viewport), color, 12);
}

void drawSegment(const Camera& camera, glm::vec2 viewport, glm::vec3 a, glm::vec3 b, glm::vec4 color) {
	const glm::vec3 delta = b - a;
	const float length = glm::length(delta);

	const glm::mat4 view = camera.getView();
	const glm::vec3 right = glm::normalize(glm::vec3(glm::row(view, 0)));
	const glm::vec3 up = glm::normalize(glm::vec3(glm::row(view, 1)));
	const float pixel = pixelSize(camera, (a + b) * 0.5f, viewport);

	if (length > k_min_length) {
		const glm::vec3 x = glm::vec3(delta.x, 0.0f, 0.0f);
		const glm::vec3 y = glm::vec3(0.0f, delta.y, 0.0f);
		const glm::vec3 z = glm::vec3(0.0f, 0.0f, delta.z);
		debug::drawLine(a, a + x, {1.0f, 0.25f, 0.25f, k_leg_alpha});
		debug::drawLine(a + x, a + x + y, {0.35f, 1.0f, 0.35f, k_leg_alpha});
		debug::drawLine(a + x + y, a + x + y + z, {0.35f, 0.55f, 1.0f, k_leg_alpha});
	}

	// Single pixel lines vanish on a busy surface so the segment is drawn as a small bundle
	const glm::vec2 offsets[] = {
	  { 0.0f,  0.0f},
    { 0.7f,  0.0f},
    {-0.7f,  0.0f},
    { 0.0f,  0.7f},
    { 0.0f, -0.7f}
	};
	for (const glm::vec2 offset : offsets) {
		const glm::vec3 shift = (right * offset.x + up * offset.y) * pixel;
		debug::drawLine(a + shift, b + shift, color);
	}

	if (length > k_min_length) {
		const glm::vec3 direction = delta / length;
		const glm::vec3 across = glm::cross(direction, camera.forward());
		if (glm::length(across) > 1e-3f) {
			const glm::vec3 tick = glm::normalize(across) * (k_tick_pixels * pixel);
			const float step = tickStep(length);
			for (int i = 1; static_cast<float>(i) * step < length - (step * 0.25f); ++i) {
				const float at = static_cast<float>(i) * step;
				const bool whole_metre = step < 1.0f && std::abs(at - std::round(at)) < 1e-3f;
				const glm::vec3 centre = a + direction * at;
				const glm::vec3 half = tick * (whole_metre ? 1.0f : 0.5f);
				debug::drawLine(centre - half, centre + half, color);
			}
		}
	}

	drawMarker(camera, viewport, a, color);
	drawMarker(camera, viewport, b, color);
}

}

void Ruler::place(glm::vec3 point) {
	if (!m_start.has_value()) {
		m_start = point;
		m_preview = point;
		return;
	}
	if (m_segments.size() >= k_max_segments) {
		m_segments.erase(m_segments.begin());
	}
	m_segments.push_back({.a = *m_start, .b = point});
	m_start.reset();
	m_preview.reset();
}

void Ruler::setPreview(std::optional<glm::vec3> point) {
	m_preview = point;
}

void Ruler::cancelPending() {
	m_start.reset();
	m_preview.reset();
}

void Ruler::removeLast() {
	if (m_start.has_value()) {
		cancelPending();
	} else if (!m_segments.empty()) {
		m_segments.pop_back();
	}
}

void Ruler::clear() {
	m_segments.clear();
	cancelPending();
}

auto Ruler::lockAxis(glm::vec3 point) const -> glm::vec3 {
	if (!m_start.has_value()) {
		return point;
	}
	const glm::vec3 delta = point - *m_start;
	const glm::vec3 magnitude = glm::abs(delta);
	int axis = 0;
	if (magnitude.y > magnitude[axis]) {
		axis = 1;
	}
	if (magnitude.z > magnitude[axis]) {
		axis = 2;
	}
	glm::vec3 locked = *m_start;
	locked[axis] = point[axis];
	return locked;
}

void Ruler::draw(const Camera& camera, glm::vec2 viewport, bool tool_active) const {
	if (viewport.x <= 0.0f || viewport.y <= 0.0f) {
		return;
	}
	for (const Segment& segment : m_segments) {
		drawSegment(camera, viewport, segment.a, segment.b, k_color);
	}
	if (tool_active && m_preview.has_value()) {
		if (m_start.has_value()) {
			drawSegment(camera, viewport, *m_start, *m_preview, k_preview_color);
		} else {
			drawMarker(camera, viewport, *m_preview, k_preview_color);
		}
	}
}

auto Ruler::snapshot(const Camera& camera, glm::vec2 viewport, bool tool_active) const -> event::RulerChanged {
	event::RulerChanged out;
	if (viewport.x <= 0.0f || viewport.y <= 0.0f) {
		return out;
	}
	out.segments.reserve(m_segments.size());
	for (const Segment& segment : m_segments) {
		out.segments.push_back(labelOf(camera, viewport, segment.a, segment.b));
	}
	if (tool_active && m_start.has_value() && m_preview.has_value()) {
		out.has_preview = true;
		out.preview = labelOf(camera, viewport, *m_start, *m_preview);
	}
	return out;
}

auto Ruler::similar(const event::RulerChanged& a, const event::RulerChanged& b) -> bool {
	const auto same = [](const event::RulerSegment& x, const event::RulerSegment& y) {
		return x.label_visible == y.label_visible && x.a == y.a && x.b == y.b && glm::distance(x.label, y.label) < k_label_epsilon;
	};
	return a.has_preview == b.has_preview && a.segments.size() == b.segments.size() &&
	       (!a.has_preview || same(a.preview, b.preview)) && std::ranges::equal(a.segments, b.segments, same);
}

}
