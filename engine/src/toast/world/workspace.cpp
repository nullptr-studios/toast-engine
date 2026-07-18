#include "workspace.hpp"

#include "camera.hpp"
#include "node.hpp"
#include "node_3d.hpp"
#include "workspace_events.hpp"
#include "workspace_events.pb.h"

#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <toast/assets/asset_manager.hpp>
#include <toast/assets/assets.hpp>
#include <toast/assets/prefab.hpp>
#include <toast/engine.hpp>
#include <toast/log.hpp>
#include <toast/renderer/vulkan_renderer.hpp>
#include <toast/scripting/asset_proxy.hpp>
#include <toast/scripting/lua_types.hpp>
#include <toast/scripting/lua_value_codec.hpp>
#include <toast/scripting/script_runtime.hpp>
#include <toast/time.hpp>
#include <toast/window/window_events.hpp>
#include <tuple>

namespace toast {

namespace {

// TODO: i didnt know where to place the ray picking shit since theres no physics class yet, MOVE THIS SOMEWHERE ELSE!

/// @brief Closest approach between two 3D lines (p1+d1*t1, p2+d2*t2)
/// @note d1/d2 must be normalized
/// @return {t1, t2}; falls back to t1=0 when the lines are parallel
auto closestPointsBetweenLines(const glm::vec3 p1, const glm::vec3 d1, const glm::vec3 p2, const glm::vec3 d2) noexcept
    -> std::pair<float, float> {
	const glm::vec3 r = p1 - p2;
	const float a = glm::dot(d1, d1);
	const float b = glm::dot(d1, d2);
	const float c = glm::dot(d2, d2);
	const float d = glm::dot(d1, r);
	const float e = glm::dot(d2, r);
	const float denom = a * c - b * b;

	if (std::abs(denom) < 1e-6f) {
		return {0.0f, c > 1e-6f ? e / c : 0.0f};
	}

	const float t1 = (b * e - c * d) / denom;
	const float t2 = (a * e - b * d) / denom;
	return {t1, t2};
}

/// @brief World-space direction for one of the 3 axis handles, shared by Translate/Rotate/Scale
auto axisDirectionFor(const GizmoHandle handle, const glm::quat& orientation) noexcept -> glm::vec3 {
	switch (handle) {
		case GizmoHandle::axis_x: return orientation * glm::vec3(1, 0, 0);
		case GizmoHandle::axis_y: return orientation * glm::vec3(0, 1, 0);
		case GizmoHandle::axis_z: return orientation * glm::vec3(0, 0, 1);
		default: return glm::vec3(0.0f);
	}
}

/// @brief Ray-plane intersection, empty if parallel or the hit would be behind the ray origin
auto rayPlaneIntersect(const Ray& ray, const glm::vec3 plane_point, const glm::vec3 plane_normal) noexcept
    -> std::optional<float> {
	const float denom = glm::dot(ray.direction, plane_normal);
	if (std::abs(denom) < 1e-6f) {
		return std::nullopt;
	}
	const float t = glm::dot(plane_point - ray.origin, plane_normal) / denom;
	if (t < 0.0f) {
		return std::nullopt;
	}
	return t;
}

struct GizmoHitResult {
	GizmoHandle handle = GizmoHandle::none;
	float t = std::numeric_limits<float>::max();
};

constexpr std::array<GizmoHandle, 3> k_axis_handles {GizmoHandle::axis_x, GizmoHandle::axis_y, GizmoHandle::axis_z};

/// @brief Ray-vs-center-handle test shared by Translate and Scale: a camera-facing
/// plane through the origin, hit accepted within k_center_hit_radius of it
///
/// Checked before the axis/plane tests, not merged into their nearest-along-ray comparison:
/// the center handle is small and sits exactly where every axis line converges, so an axis line passing
/// right behind it would otherwise win on raw ray-distance even when the cursor is squarely over the cube
auto pickCenterHandle(const Ray& ray, const glm::vec3 origin, const float scale) noexcept -> std::optional<GizmoHitResult> {
	using namespace gizmo_layout;

	const glm::vec3 to_ray_origin = ray.origin - origin;
	if (glm::length(to_ray_origin) <= 1e-4f) {
		return std::nullopt;
	}
	const glm::vec3 normal = glm::normalize(to_ray_origin);
	auto hit_t = rayPlaneIntersect(ray, origin, normal);
	if (!hit_t.has_value()) {
		return std::nullopt;
	}
	const glm::vec3 hit_point = ray.origin + ray.direction * (*hit_t);
	if (glm::distance(hit_point, origin) <= k_center_hit_radius * scale) {
		return GizmoHitResult {GizmoHandle::center, *hit_t};
	}
	return std::nullopt;
}

/// @brief Analytic ray-vs-handle test against all 7 translate-gizmo handles, keeps the nearest hit along the ray
/// Mirrors the geometry DebugPass draws (see gizmo_layout.hpp) so hit-testing never drifts from what's rendered
auto pickTranslateHandle(const Ray& ray, const glm::vec3 origin, const std::array<glm::vec3, 3>& axes, const float scale) noexcept
    -> GizmoHitResult {
	using namespace gizmo_layout;

	if (auto center_hit = pickCenterHandle(ray, origin, scale)) {
		return *center_hit;
	}

	GizmoHitResult best;

	const float axis_len = (k_shaft_length + k_head_length) * scale;
	const float axis_radius = k_axis_hit_radius * scale;

	for (int i = 0; i < 3; ++i) {
		const auto [t1, t2] = closestPointsBetweenLines(ray.origin, ray.direction, origin, axes[i]);
		if (t1 < 0.0f || t2 < 0.0f || t2 > axis_len) {
			continue;
		}
		const glm::vec3 ray_point = ray.origin + ray.direction * t1;
		const glm::vec3 axis_point = origin + axes[i] * t2;
		if (glm::distance(ray_point, axis_point) > axis_radius) {
			continue;
		}
		if (t1 < best.t) {
			best = {k_axis_handles[i], t1};
		}
	}

	const std::array<std::tuple<GizmoHandle, int, int>, 3> planes {
	  {{GizmoHandle::plane_xy, 0, 1}, {GizmoHandle::plane_yz, 1, 2}, {GizmoHandle::plane_xz, 0, 2}}
	};

	for (const auto& [handle, u, v] : planes) {
		const glm::vec3 normal = glm::normalize(glm::cross(axes[u], axes[v]));
		auto hit_t = rayPlaneIntersect(ray, origin, normal);
		if (!hit_t.has_value()) {
			continue;
		}
		const glm::vec3 hit_point = ray.origin + ray.direction * (*hit_t);
		const float local_u = glm::dot(hit_point - origin, axes[u]);
		const float local_v = glm::dot(hit_point - origin, axes[v]);
		const float lo = k_plane_offset * scale;
		const float hi = (k_plane_offset + k_plane_size) * scale;
		if (local_u < lo || local_u > hi || local_v < lo || local_v > hi) {
			continue;
		}
		if (*hit_t < best.t) {
			best = {handle, *hit_t};
		}
	}

	return best;
}

/// @brief Ray-vs-ring test, each ring lies flat in the plane whose normal is its own axis; a hit is accepted
/// when the ray-plane intersection lands within the ring's radial band
auto pickRotateHandle(const Ray& ray, const glm::vec3 origin, const std::array<glm::vec3, 3>& axes, const float scale) noexcept
    -> GizmoHitResult {
	using namespace gizmo_layout;

	GizmoHitResult best;
	const float radius = k_ring_radius * scale;
	const float band = k_ring_thickness * scale;

	for (int i = 0; i < 3; ++i) {
		auto hit_t = rayPlaneIntersect(ray, origin, axes[i]);
		if (!hit_t.has_value()) {
			continue;
		}
		const glm::vec3 hit_point = ray.origin + ray.direction * (*hit_t);
		if (std::abs(glm::distance(hit_point, origin) - radius) > band) {
			continue;
		}
		if (*hit_t < best.t) {
			best = {k_axis_handles[i], *hit_t};
		}
	}

	return best;
}

/// @brief Ray-vs-handle test for Scale, shorter axis segments (shaft + cube head) plus the shared center handle
auto pickScaleHandle(const Ray& ray, const glm::vec3 origin, const std::array<glm::vec3, 3>& axes, const float scale) noexcept
    -> GizmoHitResult {
	using namespace gizmo_layout;

	if (auto center_hit = pickCenterHandle(ray, origin, scale)) {
		return *center_hit;
	}

	GizmoHitResult best;
	const float axis_len = (k_shaft_length + 2.0f * k_scale_head_half_size) * scale;
	const float axis_radius = k_axis_hit_radius * scale;

	for (int i = 0; i < 3; ++i) {
		const auto [t1, t2] = closestPointsBetweenLines(ray.origin, ray.direction, origin, axes[i]);
		if (t1 < 0.0f || t2 < 0.0f || t2 > axis_len) {
			continue;
		}
		const glm::vec3 ray_point = ray.origin + ray.direction * t1;
		const glm::vec3 axis_point = origin + axes[i] * t2;
		if (glm::distance(ray_point, axis_point) > axis_radius) {
			continue;
		}
		if (t1 < best.t) {
			best = {k_axis_handles[i], t1};
		}
	}

	return best;
}

/// @brief Dispatches to the hit-test for whichever tool is active
auto pickGizmoHandle(
    GizmoTool tool, const Ray& ray, const glm::vec3 origin, const glm::quat orientation, const float scale
) noexcept -> GizmoHitResult {
	const std::array<glm::vec3, 3> axes {
	  orientation * glm::vec3(1, 0, 0),
	  orientation * glm::vec3(0, 1, 0),
	  orientation * glm::vec3(0, 0, 1),
	};

	switch (tool) {
		case GizmoTool::translate: return pickTranslateHandle(ray, origin, axes, scale);
		case GizmoTool::rotate: return pickRotateHandle(ray, origin, axes, scale);
		case GizmoTool::scale: return pickScaleHandle(ray, origin, axes, scale);
		default: return {};
	}
}

}    // namespace

struct VectorStreamBuf : std::streambuf {
	VectorStreamBuf(const std::vector<uint8_t>& vec) {
		char* p = const_cast<char*>(reinterpret_cast<const char*>(vec.data()));
		setg(p, p, p + vec.size());
	}
};

using scripting::LuaVarDesc;
using scripting::LuaVarKind;
using scripting::parseLuaValue;
using scripting::stringifyLuaValue;

static std::unique_ptr<assets::Prefab> s_clipboard;

Workspace::Workspace(UID handle, EmptyTag) : m_handle(handle) {
	eventSubscriptions();
}

Workspace::Workspace(std::string_view type, UID handle) : m_handle(handle) {
	ZoneScoped;
	eventSubscriptions();

	// Allocation
	Box node = this->nodeAllocation(type);
	node->propagateCallTick(node->info(), TickFunctionList::pre_init);
	node->propagateCallTick(node->info(), TickFunctionList::load);

	// Data structure generation
	generateUid(node);
	node->m_parent = {};
	node->m_state = NodeState::root;
	node->m_type = NodeType::world_root;
	node->m_inherited_enabled = true;
	node->m_name = stripNamespace(node->info()->type);

	// Initialization
	node->propagateCallTick(node->info(), TickFunctionList::init);
	node->propagateCallTick(node->info(), TickFunctionList::begin);
	node->m_local_enabled = true;
	node->propagateEnable();

	m_root_node = node;
	TOAST_INFO("World", "Created new workspace");
}

Workspace::Workspace(UID uid) : m_handle(uid) {
	eventSubscriptions();

	// open file
	auto file = assets::load<assets::Prefab>(uid);
	if (not file.hasValue()) {
		TOAST_ERROR("World", "Couldn't open Node file {}", uid);
		return;
	}

	initFromPrefab(file);
	if (m_root_node.exists()) {
		TOAST_INFO("World", "Opened workspace from {}", uid);
	}
}

Workspace::Workspace(UID uid, std::string_view source_uri) : m_handle(uid) {
	eventSubscriptions();

	auto bytes = assets::AssetManager::get().loadBytes(source_uri);
	if (not bytes.has_value()) {
		TOAST_ERROR("World", "Couldn't open workspace source file {}", source_uri);
		return;
	}

	// We need this because if we use the vector<uint8> constructor is going to try
	// to load the node as a .tbnode rather than as a .tnode, we need to be careful with that
	VectorStreamBuf buffer(*bytes);
	std::istream autosave(&buffer);
	assets::Prefab prefab(autosave);
	assets::Handle<assets::Prefab> file(&prefab, uid, "");
	initFromPrefab(file);
	if (m_root_node.exists()) {
		TOAST_INFO("World", "Opened workspace {} from {}", uid, source_uri);
	}
}

void Workspace::initFromPrefab(const assets::Handle<assets::Prefab>& file) {
	INodeOwner::InstantiateContext ctx;
	ctx.resolver = [](toast::UID id) { return assets::load<assets::Prefab>(id); };
	Box<Node> node = instantiate(file, ctx);
	if (not node.exists()) {
		TOAST_ERROR("World", "Failed to instantiate node {}", m_handle);
		return;
	}
	// node->propagateCallTick(node->info(), TickFunctionList::load);
	// node->propagateCallTick(node->info(), TickFunctionList::pre_init);

	// Data structure generation
	generateUid(node);
	node->m_parent = {};
	node->m_state = NodeState::root;
	node->m_type = NodeType::world_root;
	node->m_inherited_enabled = true;

	// Initialization
	node->propagateCallTick(node->info(), TickFunctionList::init);
	node->propagateCallTick(node->info(), TickFunctionList::begin);
	node->m_local_enabled = true;
	node->propagateEnable();

	m_root_node = node;
}

Workspace::~Workspace() {
	if (!m_root_node.exists()) {
		return;
	}

	m_root_node->propagateCallTick(m_root_node->info(), TickFunctionList::on_disable);
	m_root_node->propagateCallTick(m_root_node->info(), TickFunctionList::end);
	m_root_node->propagateCallTick(m_root_node->info(), TickFunctionList::destroy);

	std::vector<Node*> victims;
	auto collect = [&victims](this auto&& self, Node& n) -> void {
		victims.push_back(&n);
		for (auto& c : n.m_children) {
			self(*c);
		}
	};
	collect(*m_root_node);
	m_root_node = {};
	m_focused_node = {};

	for (Node* victim : victims) {
		_detail::ControlBox* control = _detail::ControlBox::get(victim);
		const NodeInfo* info = victim->info();
		victim->m_parent = {};
		victim->m_children.clear();
		victim->m_listener.reset();
		if (info && info->destroy) {
			info->destroy(victim);
		} else {
			delete victim;
		}
		releaseNode(*control);
	}
	reapTombstones();
}

auto Workspace::name() -> std::string {
	if (!m_root_node.exists()) {
		return "";
	}
	return std::string {m_root_node->name()};
}

void Workspace::registerDependency(Node& from, Node& to) {
	// TODO:
	// TOAST_NOT_IMPLEMENTED;
}

void Workspace::unregisterDependency(Node& from, Node& to) { }

auto Workspace::findFrom(const Node& origin, std::string_view query) -> Box<Node> {
	auto search = [query](this auto&& self, const Node& node) -> Box<Node> {
		if (node.name() == query) {
			return node.box();
		}
		for (const auto& c : node.m_children) {
			if (auto found = self(*c); found.exists()) {
				return found;
			}
		}
		return {};
	};

	return search(origin);
}

auto Workspace::findFrom(const Node& origin, const UID& uid) -> Box<Node> {
	auto search = [target = uid.data()](this auto&& self, const Node& node) -> Box<Node> {
		if (node.m_uid.data() == target) {
			return node.box();
		}
		for (const auto& c : node.m_children) {
			if (auto found = self(*c); found.exists()) {
				return found;
			}
		}
		return {};
	};

	return search(origin);
}

auto Workspace::searchFrom(const Node& origin, std::string_view query) -> std::vector<Box<Node>> {
	// TODO:
	TOAST_NOT_IMPLEMENTED;
	return {};
}

auto Workspace::gizmoOrigin() const -> glm::vec3 {
	auto node3d = m_focused_node.as<Node3D>();
	return node3d.exists() ? node3d->worldPos() : glm::vec3(0.0f);
}

auto Workspace::gizmoOrientation() const -> glm::quat {
	if (m_world_space) {
		return {1.0f, 0.0f, 0.0f, 0.0f};
	}
	auto node3d = m_focused_node.as<Node3D>();
	return node3d.exists() ? node3d->worldRotQuat() : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

auto Workspace::gizmoScale() const -> float {
	Camera* camera = renderer::getActiveCamera();
	if (camera == nullptr) {
		return 1.0f;
	}
	return gizmo_layout::k_screen_size * glm::distance(camera->worldPos(), gizmoOrigin());
}

void Workspace::gizmoUpdateHover() {
	// mid-drag, the grabbed handle stays active regardless of what the cursor is over
	if (m_gizmo_drag != GizmoHandle::none) {
		return;
	}

	const bool tool_has_gizmo =
	    m_gizmo_tool == GizmoTool::translate || m_gizmo_tool == GizmoTool::rotate || m_gizmo_tool == GizmoTool::scale;
	if (not tool_has_gizmo || not m_focused_node.as<Node3D>().exists()) {
		m_gizmo_hover = GizmoHandle::none;
		return;
	}

	Camera* camera = renderer::getActiveCamera();
	if (camera == nullptr) {
		m_gizmo_hover = GizmoHandle::none;
		return;
	}

	const auto extent = renderer::getOutputTarget().getExtent();
	const glm::vec2 viewport_size {static_cast<float>(extent.width), static_cast<float>(extent.height)};
	const Ray ray = camera->screenPointToRay(m_gizmo_mouse_pos, viewport_size);

	m_gizmo_hover = pickGizmoHandle(m_gizmo_tool, ray, gizmoOrigin(), gizmoOrientation(), gizmoScale()).handle;
}

void Workspace::gizmoBeginDrag(GizmoHandle handle) {
	auto node3d = m_focused_node.as<Node3D>();
	Camera* camera = renderer::getActiveCamera();
	if (not node3d.exists() || handle == GizmoHandle::none || camera == nullptr) {
		return;
	}

	m_gizmo_drag = handle;
	m_gizmo_drag_start_world_pos = node3d->worldPos();
	m_gizmo_drag_start_rotation = node3d->worldRotQuat();
	m_gizmo_drag_start_scale = node3d->worldScale();
	m_gizmo_drag_current_factor = 1.0f;

	const glm::vec3 origin = m_gizmo_drag_start_world_pos;
	const glm::quat orientation = gizmoOrientation();
	const bool is_axis = handle == GizmoHandle::axis_x || handle == GizmoHandle::axis_y || handle == GizmoHandle::axis_z;

	const auto extent = renderer::getOutputTarget().getExtent();
	const glm::vec2 viewport_size {static_cast<float>(extent.width), static_cast<float>(extent.height)};
	const Ray ray = camera->screenPointToRay(m_gizmo_mouse_pos, viewport_size);

	if (m_gizmo_tool == GizmoTool::rotate && is_axis) {
		m_gizmo_drag_axis = axisDirectionFor(handle, orientation);
		m_gizmo_drag_plane_normal = m_gizmo_drag_axis;

		const auto [basis_u, basis_v] = gizmo_layout::ringBasis(m_gizmo_drag_axis);

		auto hit_t = rayPlaneIntersect(ray, origin, m_gizmo_drag_plane_normal);
		const glm::vec3 hit = hit_t.has_value() ? (ray.origin + ray.direction * (*hit_t) - origin) : basis_u;
		m_gizmo_drag_start_angle = std::atan2(glm::dot(hit, basis_v), glm::dot(hit, basis_u));
		return;
	}

	if (handle == GizmoHandle::center) {
		m_gizmo_drag_plane_normal = glm::normalize(camera->worldPos() - origin);
	} else if (is_axis) {
		m_gizmo_drag_axis = axisDirectionFor(handle, orientation);
	} else {
		switch (handle) {
			case GizmoHandle::plane_xy: m_gizmo_drag_plane_normal = orientation * glm::vec3(0, 0, 1); break;
			case GizmoHandle::plane_yz: m_gizmo_drag_plane_normal = orientation * glm::vec3(1, 0, 0); break;
			case GizmoHandle::plane_xz: m_gizmo_drag_plane_normal = orientation * glm::vec3(0, 1, 0); break;
			default: break;
		}
	}

	if (is_axis) {
		const auto [_, t2] = closestPointsBetweenLines(ray.origin, ray.direction, origin, m_gizmo_drag_axis);
		m_gizmo_drag_anchor = origin + m_gizmo_drag_axis * t2;
	} else {
		auto hit_t = rayPlaneIntersect(ray, origin, m_gizmo_drag_plane_normal);
		m_gizmo_drag_anchor = hit_t.has_value() ? ray.origin + ray.direction * (*hit_t) : origin;
	}
}

void Workspace::gizmoUpdateDrag() {
	if (m_gizmo_drag == GizmoHandle::none) {
		return;
	}

	auto node3d = m_focused_node.as<Node3D>();
	Camera* camera = renderer::getActiveCamera();
	if (not node3d.exists() || camera == nullptr) {
		gizmoEndDrag();
		return;
	}

	const auto extent = renderer::getOutputTarget().getExtent();
	const glm::vec2 viewport_size {static_cast<float>(extent.width), static_cast<float>(extent.height)};
	const Ray ray = camera->screenPointToRay(m_gizmo_mouse_pos, viewport_size);
	const glm::vec3 origin = m_gizmo_drag_start_world_pos;
	const bool is_axis =
	    m_gizmo_drag == GizmoHandle::axis_x || m_gizmo_drag == GizmoHandle::axis_y || m_gizmo_drag == GizmoHandle::axis_z;

	if (m_gizmo_tool == GizmoTool::rotate) {
		auto hit_t = rayPlaneIntersect(ray, origin, m_gizmo_drag_plane_normal);
		if (not hit_t.has_value()) {
			return;
		}
		const auto [basis_u, basis_v] = gizmo_layout::ringBasis(m_gizmo_drag_axis);
		const glm::vec3 hit = ray.origin + ray.direction * (*hit_t) - origin;
		const float angle = std::atan2(glm::dot(hit, basis_v), glm::dot(hit, basis_u));

		float delta_angle = angle - m_gizmo_drag_start_angle;
		if (m_rotate_snap.enabled && m_rotate_snap.value > 0.0001f) {
			const float step = glm::radians(m_rotate_snap.value);
			delta_angle = std::round(delta_angle / step) * step;
		}

		node3d->worldRotQuat(glm::normalize(glm::angleAxis(delta_angle, m_gizmo_drag_axis) * m_gizmo_drag_start_rotation));
		return;
	}

	if (m_gizmo_tool == GizmoTool::scale) {
		float delta_scalar = 0.0f;
		if (m_gizmo_drag == GizmoHandle::center) {
			auto hit_t = rayPlaneIntersect(ray, origin, m_gizmo_drag_plane_normal);
			if (not hit_t.has_value()) {
				return;
			}
			const glm::vec3 current = ray.origin + ray.direction * (*hit_t);
			delta_scalar = glm::dot(current - m_gizmo_drag_anchor, camera->up());
		} else if (is_axis) {
			const auto [_, t2] = closestPointsBetweenLines(ray.origin, ray.direction, origin, m_gizmo_drag_axis);
			const glm::vec3 current = origin + m_gizmo_drag_axis * t2;
			delta_scalar = glm::dot(current - m_gizmo_drag_anchor, m_gizmo_drag_axis);
		} else {
			return;
		}

		// delta_scalar is a world-space distance, convert to a multiplicative factor relative to the
		// gizmos own on-screen size so a given drag distance feels the same regardless of camera distance
		const float reference = std::max(gizmoScale(), 0.0001f);
		float factor = 1.0f + delta_scalar / reference;
		if (m_scale_snap.enabled && m_scale_snap.value > 0.0001f) {
			const float s = m_scale_snap.value;
			factor = std::round(factor / s) * s;
		}
		factor = std::max(factor, 0.01f);
		m_gizmo_drag_current_factor = factor;

		glm::vec3 new_scale = m_gizmo_drag_start_scale;
		if (m_gizmo_drag == GizmoHandle::center) {
			new_scale = m_gizmo_drag_start_scale * factor;
		} else {
			const auto axis_index = static_cast<int>(m_gizmo_drag) - static_cast<int>(GizmoHandle::axis_x);
			new_scale[axis_index] = m_gizmo_drag_start_scale[axis_index] * factor;
		}

		node3d->worldScale(new_scale);
		return;
	}

	// translate
	glm::vec3 delta {0.0f};
	if (is_axis) {
		const auto [_, t2] = closestPointsBetweenLines(ray.origin, ray.direction, origin, m_gizmo_drag_axis);
		const glm::vec3 current = origin + m_gizmo_drag_axis * t2;
		delta = glm::dot(current - m_gizmo_drag_anchor, m_gizmo_drag_axis) * m_gizmo_drag_axis;
	} else {
		auto hit_t = rayPlaneIntersect(ray, origin, m_gizmo_drag_plane_normal);
		if (not hit_t.has_value()) {
			return;
		}
		delta = (ray.origin + ray.direction * (*hit_t)) - m_gizmo_drag_anchor;
	}

	if (m_translate_snap.enabled && m_translate_snap.value > 0.0001f) {
		const float s = m_translate_snap.value;
		delta = glm::round(delta / s) * s;
	}

	node3d->worldPos(m_gizmo_drag_start_world_pos + delta);
}

void Workspace::gizmoEndDrag() {
	m_gizmo_drag = GizmoHandle::none;
}

void Workspace::eventSubscriptions() {
	// Whenever we get notified that the hierarchy needs an update, send the update back to the editor
	m_listener.subscribe<event::RequestHierarchyUpdate>(
	    [this] {
		    if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			    return false;
		    }

		    event::send<event::UpdateHierarchyData>(m_root_node);
		    return true;
	    },
	    100
	);

	m_listener.subscribe<event::WorkspaceCreateNode>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		TOAST_ASSERT(m_root_node.exists(), "World", "Trying to add a node to an invalid Workspace");

		auto parent = findFrom(m_root_node, e.parent);
		if (not parent.exists()) {
			TOAST_WARN("World", "Tried to create node on Workspace {} but parent couldn't be found", m_root_node->name());
			return true;
		}

		auto node = requestRuntimeCreate(parent, e.type);
		TOAST_INFO("World", "Created node {} in Workspace {}", node->name(), m_root_node->name());
		event::send<event::RequestHierarchyUpdate>();
		return true;
	});

	m_listener.subscribe<event::WorkspaceRemoveNode>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		TOAST_ASSERT(m_root_node.exists(), "World", "Trying to add a node to an invalid Workspace");

		auto node = findFrom(m_root_node, e.target);
		if (not node.exists() || not node->parentInternal().exists()) {
			TOAST_WARN("World", "Tried to remove node on Workspace {} but the node couldn't be found", m_root_node->name());
			return true;
		}

		auto parent = node->parentInternal();
		auto name = std::string {node->name()};

		// Detach from the parent so the editor no longer reaches the subtree
		std::erase(parent->m_children, node);

		// Collect the whole subtree into raw pointers
		std::vector<Node*> victims;
		auto collect = [&victims](this auto&& self, Node& n) -> void {
			victims.push_back(&n);
			for (auto& c : n.m_children) {
				self(*c);
			}
		};
		collect(*node);
		node = {};    // drop our own reference; nothing external holds the subtree now

		// Free every node in place
		for (Node* victim : victims) {
			_detail::ControlBox* control = _detail::ControlBox::get(victim);
			const NodeInfo* info = victim->info();

			victim->m_parent = {};
			victim->m_children.clear();
			victim->m_listener.reset();

			if (info && info->destroy) {
				info->destroy(victim);
			} else {
				delete victim;
			}
			releaseNode(*control);
		}
		reapTombstones();

		event::send<event::RequestHierarchyUpdate>();
		TOAST_INFO("World", "Removed node {} in Workspace {}", name, m_root_node->name());
		return true;
	});

	m_listener.subscribe<event::WorkspaceSave>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		auto node = findFrom(m_root_node, e.target);
		if (not node.exists()) {
			TOAST_WARN("World", "Tried to save workspace but the target node couldn't be found");
			return true;
		}

		// TODO: rename the root node to the file stem once SetNodeParameter exists
		assets::Prefab prefab(*node);
		auto bytes = prefab.serialize(assets::SaveMode::editor);
		if (assets::AssetManager::get().saveBytes(e.uri, bytes)) {
			TOAST_INFO("World", "Saved workspace {} to {}", node->name(), e.uri);
		}

		event::send<event::ReloadAssetsManifest>();
		return true;
	});

	// Unlike WorkspaceSave this matches on the workspace handle, so background
	// workspaces autosave too, and it never touches the asset manifest
	m_listener.subscribe<event::WorkspaceAutosave>([this](const auto& e) {
		if (e.handle != m_handle.data()) {
			return false;
		}
		if (not m_root_node.exists()) {
			return true;
		}

		assets::Prefab prefab(*m_root_node);
		auto bytes = prefab.serialize(assets::SaveMode::editor);
		if (assets::AssetManager::get().saveBytes(e.uri, bytes)) {
			TOAST_INFO("World", "Autosaved workspace {} to {}", m_root_node->name(), e.uri);
		}
		return true;
	});

	m_listener.subscribe<event::WorkspaceMoveNodeTo>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		TOAST_ASSERT(m_root_node.exists(), "World", "Trying to add a node to an invalid Workspace");

		auto node = findFrom(m_root_node, e.target);
		if (not node.exists() || node == m_root_node) {
			TOAST_WARN("World", "Tried to move node on Workspace {} but the node couldn't be found", m_root_node->name());
			return true;
		}

		// An empty new parent means "keep the current parent"
		auto dest_parent = e.new_parent.data() == 0 ? node->parentInternal() : findFrom(m_root_node, e.new_parent);
		if (not dest_parent.exists()) {
			TOAST_WARN("World", "Couldn't find new parent on Workspace {}", m_root_node->name());
			return true;
		}

		// Reject reparenting a node into itself or one of its descendants
		if (findFrom(node, dest_parent->uid()).exists()) {
			TOAST_WARN("World", "Tried to move node {} into its own descendant", node->name());
			return true;
		}

		// Detach from the current parent
		if (auto old_parent = node->parentInternal(); old_parent.exists()) {
			std::erase(old_parent->m_children, node);
		}

		node->m_parent = dest_parent;

		// Insert at the position requested by the predecessor uid
		auto& children = dest_parent->m_children;
		uint64_t pred = e.predecessor.data();
		if (pred == 0) {
			children.insert(children.begin(), node);
		} else if (pred == std::numeric_limits<uint64_t>::max()) {
			children.push_back(node);
		} else {
			auto it = std::ranges::find_if(children, [pred](const Box<Node>& c) { return c->m_uid.data() == pred; });
			if (it != children.end()) {
				children.insert(it + 1, node);
			} else {
				children.push_back(node);
			}
		}

		event::send<event::RequestHierarchyUpdate>();
		TOAST_INFO("World", "Moved node {} in Workspace {}", node->name(), m_root_node->name());
		return true;
	});

	m_listener.subscribe<event::SetFocusedNode>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		if (!m_root_node.exists()) {
			return false;
		}
		m_focused_node = findFrom(m_root_node, e.node);
		return false;
	});

	m_listener.subscribe<event::NodeChangeParam>([this](const auto& e) {
		// Only the workspace that actually owns the focused node applies the change
		if (not m_focused_node.exists()) {
			return false;
		}

		const auto* field = m_focused_node->info()->search(e.parameter);
		if (field == nullptr) {
			TOAST_WARN("World", "NodeChangeParam: unknown parameter '{}'", e.parameter);
			return true;
		}

		std::any value;
		if (field->value_type == FieldType::quaternion_t && not field->is_array) {
			// the inspector edits rotation as euler degrees "x y z"; store it back as a quaternion
			glm::vec3 deg {0.0f, 0.0f, 0.0f};
			std::istringstream ss(e.value);
			ss >> deg.x >> deg.y >> deg.z;
			value = std::any {glm::quat(glm::radians(deg))};
		} else {
			auto parsed = assets::Prefab::valueFromString(field->value_type, field->is_array, e.value);
			if (not parsed.has_value()) {
				TOAST_WARN("World", "NodeChangeParam: couldn't parse '{}' for parameter '{}'", e.value, e.parameter);
				return true;
			}
			value = std::move(*parsed);
		}

		field->set(&*m_focused_node, value);

		if (field->name == "m_scripts") {
			m_focused_node->reloadScripts();
			event::send<event::RequestHierarchyUpdate>();
		}
		return true;
	});

	// Lua variable edits address "<instance>:group/subgroup/name" through the script schema
	m_listener.subscribe<event::NodeChangeLuaParam>([this](const auto& e) {
		if (not m_focused_node.exists()) {
			return false;
		}
		scripting::ScriptRuntime* rt = m_focused_node->scriptRuntime();
		if (rt == nullptr) {
			return true;
		}

		const size_t colon = e.path.find(':');
		size_t instance = 0;
		if (colon == std::string::npos || std::from_chars(e.path.data(), e.path.data() + colon, instance).ec != std::errc {}) {
			TOAST_WARN("World", "NodeChangeLuaParam: malformed path '{}'", e.path);
			return true;
		}
		const auto var_path = std::string_view(e.path).substr(colon + 1);

		const scripting::ScriptSchema* schema = rt->instanceSchema(instance);
		const LuaVarDesc* desc = schema != nullptr ? schema->find(var_path) : nullptr;
		if (desc == nullptr) {
			TOAST_WARN("World", "NodeChangeLuaParam: unknown variable '{}'", e.path);
			return true;
		}

		auto find_node = [this](std::string_view uid_text) -> Box<Node> {
			if (uid_text.empty()) {
				return {};
			}
			UID uid(toast::UID::fromString(std::string(uid_text)));
			if (uid.data() == 0) {
				return {};
			}
			return findFrom(m_root_node, uid);
		};

		std::any value = parseLuaValue(*desc, e.value, find_node);
		if (value.has_value()) {
			rt->setVarByPath(instance, var_path, value);
		} else {
			TOAST_WARN("World", "NodeChangeLuaParam: couldn't parse '{}' for '{}'", e.value, e.path);
		}
		return true;
	});

	// Renames go through their own event so the engine is the single source of truth and can refresh
	// the hierarchy itself, rather than the editor mutating the name and forcing an update
	m_listener.subscribe<event::NodeChangeName>([this](const auto& e) {
		if (not m_root_node.exists()) {
			return false;
		}
		auto node = findFrom(m_root_node, e.node);
		if (not node.exists()) {
			return false;
		}
		node->m_name = e.name;
		event::send<event::RequestHierarchyUpdate>();
		return true;
	});

	// TODO: needs function reflection for this
	// m_listener.subscribe<event::NodeCallFunction>([this](const auto& e) {
	// 	m_focused_node->info()->call(e.function);
	// });

	m_listener.subscribe<event::NodeEnabled>([this](const auto& e) {
		if (not m_root_node.exists()) {
			return false;
		}
		auto node = findFrom(m_root_node, e.node);
		if (not node.exists()) {
			return false;
		}
		node->enabled(e.enabled);
		event::send<event::RequestHierarchyUpdate>();
		return true;
	});

	m_listener.subscribe<event::WorkspaceSpawn>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		TOAST_ASSERT(m_root_node.exists(), "World", "Trying to spawn a node in an invalid Workspace");

		auto parent = findFrom(m_root_node, e.parent);
		if (not parent.exists()) {
			TOAST_WARN("World", "WorkspaceSpawn: parent {} not found", e.parent);
			return true;
		}

		// requestRuntimeSpawn sends RequestHierarchyUpdate on success
		if (e.is_uri) {
			requestRuntimeSpawn(parent, e.uri);
		} else {
			requestRuntimeSpawn(parent, e.uid);
		}
		return true;
	});

	m_listener.subscribe<event::WorkspaceDuplicateNode>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}

		auto src = findFrom(m_root_node, e.source);
		auto par = findFrom(m_root_node, e.parent);
		if (not src.exists() || not par.exists()) {
			TOAST_WARN("World", "WorkspaceDuplicateNode: source or parent not found");
			return true;
		}

		assets::Prefab prefab(*src);
		assets::Handle<assets::Prefab> handle(&prefab, toast::UID(0), "");

		InstantiateContext ctx;
		ctx.resolver = [](toast::UID id) { return assets::load<assets::Prefab>(id); };
		Box<Node> copy = instantiate(handle, ctx);
		if (not copy.exists()) {
			TOAST_WARN("World", "WorkspaceDuplicateNode: instantiation failed");
			return true;
		}

		// Regenerate UIDs on every node in the copy to avoid collisions with the originals
		auto regen = [&](auto& self, Box<Node>& node) -> void {
			INodeOwner::generateUid(node);
			for (auto& child : node->m_children) {
				self(self, child);
			}
		};
		regen(regen, copy);

		// The in-memory prefab has no meaningful UID, so clear the root's source_prefab
		// to avoid marking the copy as a prefab instance with UID 0
		copy->m_source_prefab = {};

		// Ensure the copy doesn't share its name with an existing sibling
		copy->m_name = uniqueChildName(*par, copy->name());

		copy->m_parent = par;
		par->m_children.emplace_back(copy);
		copy->m_state = par->m_state;
		copy->m_type = NodeType::child;
		copy->m_inherited_enabled = par->enabled();

		copy->propagateCallTick(copy->info(), TickFunctionList::init);
		copy->propagateCallTick(copy->info(), TickFunctionList::begin);
		copy->enabled(true);

		event::send<event::RequestHierarchyUpdate>();
		TOAST_INFO("World", "Duplicated {} under {}", src->name(), par->name());
		return true;
	});

	m_listener.subscribe<event::NodeChangeType>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}

		auto target = findFrom(m_root_node, e.node);
		if (not target.exists()) {
			TOAST_WARN("World", "NodeChangeType: node not found");
			return true;
		}

		auto parent = target->parentInternal();
		if (not parent.exists()) {
			TOAST_WARN("World", "NodeChangeType: cannot change type of root node");
			return true;
		}

		// Allocate the replacement node
		Box<Node> fresh = nodeAllocation(e.type);
		fresh->propagateCallTick(fresh->info(), TickFunctionList::pre_init);

		fresh->m_uid = target->m_uid;
		fresh->m_name = target->m_name;
		fresh->m_state = target->m_state;
		fresh->m_type = target->m_type;
		fresh->m_inherited_enabled = target->m_inherited_enabled;

		// Transfer children from the old node to the new one
		for (auto& child : target->m_children) {
			child->m_parent = fresh;
			fresh->m_children.push_back(std::move(child));
		}
		target->m_children.clear();

		// Replace the old node in the parent's children list
		fresh->m_parent = parent;
		auto& siblings = parent->m_children;
		auto it = std::ranges::find(siblings, target);
		if (it != siblings.end()) {
			*it = fresh;
		}

		// Destroy the old node using the same pattern as WorkspaceRemoveNode
		Node* old_raw = &*target;
		_detail::ControlBox* old_ctrl = _detail::ControlBox::get(old_raw);
		const NodeInfo* old_info = old_raw->info();
		old_raw->m_parent = {};
		old_raw->m_listener.reset();
		target = {};

		if (old_info && old_info->destroy) {
			old_info->destroy(old_raw);
		} else {
			delete old_raw;
		}
		releaseNode(*old_ctrl);
		reapTombstones();

		// Initialize the fresh node
		fresh->callTick(fresh->info(), TickFunctionList::init);
		fresh->callTick(fresh->info(), TickFunctionList::begin);
		fresh->enabled(true);

		event::send<event::RequestHierarchyUpdate>();
		TOAST_INFO("World", "Changed node type to {}", e.type);
		return true;
	});

	// C# side creates the empty file + meta + rebuilds the asset database before sending this
	// Here we write the actual node content and replace the inline subtree with a prefab reference
	// Asset creation is becoming an interconnected mess of c++ and c# and im starting to get mad -x
	m_listener.subscribe<event::WorkspacePromoteNode>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}

		auto target = findFrom(m_root_node, e.target);
		if (not target.exists()) {
			TOAST_WARN("World", "WorkspacePromoteNode: target not found");
			return true;
		}

		auto parent = target->parentInternal();
		if (not parent.exists()) {
			TOAST_WARN("World", "WorkspacePromoteNode: cannot promote root node");
			return true;
		}

		// Write the node content to the file the C# side already created on disk
		assets::Prefab prefab(*target);
		auto bytes = prefab.serialize(assets::SaveMode::editor);
		assets::AssetManager::get().saveBytes(e.path, bytes);

		std::erase(parent->m_children, target);

		std::vector<Node*> victims;
		auto collect = [&victims](this auto&& self, Node& n) -> void {
			victims.push_back(&n);
			for (auto& c : n.m_children) {
				self(*c);
			}
		};
		collect(*target);
		target = {};

		for (Node* victim : victims) {
			_detail::ControlBox* ctrl = _detail::ControlBox::get(victim);
			const NodeInfo* info = victim->info();
			victim->m_parent = {};
			victim->m_children.clear();
			victim->m_listener.reset();
			if (info && info->destroy) {
				info->destroy(victim);
			} else {
				delete victim;
			}
			releaseNode(*ctrl);
		}
		reapTombstones();

		// Spawn the saved file as a prefab child of the same parent
		auto uid = assets::resolveURI(e.path);
		if (uid.has_value()) {
			requestRuntimeSpawn(parent, *uid);
		} else {
			TOAST_WARN("World", "WorkspacePromoteNode: couldn't resolve UID for {}", e.path);
			event::send<event::RequestHierarchyUpdate>();
		}

		TOAST_INFO("World", "Promoted node to {}", e.path);
		return true;
	});

	// mirrored editor toolbar state; the active workspace is the one being edited
	m_listener.subscribe<event::SetGizmoTool>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		m_gizmo_tool = static_cast<GizmoTool>(e.tool);
		return true;
	});

	m_listener.subscribe<event::SetCoordinateSpace>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		m_world_space = e.world;
		return true;
	});

	m_listener.subscribe<event::SetSnapping>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		SnapSetting setting {e.enabled, e.value};
		switch (e.kind) {
			case 0: m_translate_snap = setting; break;
			case 1: m_rotate_snap = setting; break;
			case 2: m_scale_snap = setting; break;
			default: TOAST_WARN("World", "SetSnapping: unknown kind {}", e.kind); break;
		}
		return true;
	});

	m_listener.subscribe<event::SetCameraMode>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		m_game_camera = e.game;
		if (e.game) {
			// entering play, drop any in-progress gizmo interaction
			m_gizmo_hover = GizmoHandle::none;
			m_gizmo_drag = GizmoHandle::none;
		}
		return true;
	});

	// Translate-gizmo interaction, driven straight off the raw window mouse events already forwarded by the editor in edit mode
	m_listener.subscribe<event::WindowMousePosition>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data() || m_game_camera || isPlaying()) {
			return false;
		}
		m_gizmo_mouse_pos = {e.x, e.y};
		if (m_gizmo_drag != GizmoHandle::none) {
			gizmoUpdateDrag();
		} else {
			gizmoUpdateHover();
		}
		return false;
	});

	m_listener.subscribe<event::WindowMouseButton>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data() || m_game_camera || isPlaying()) {
			return false;
		}
		if (e.button != 1) {
			return false;
		}
		if (e.action == event::window_input_pressed && m_gizmo_hover != GizmoHandle::none) {
			gizmoBeginDrag(m_gizmo_hover);
		} else if (e.action == event::window_input_released) {
			gizmoEndDrag();
		}
		return false;
	});

	m_listener.subscribe<event::WorkspaceCopyNode>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		auto src = findFrom(m_root_node, e.source);
		if (not src.exists()) {
			TOAST_WARN("World", "WorkspaceCopyNode: source not found");
			return true;
		}
		s_clipboard = std::make_unique<assets::Prefab>(*src);
		return true;
	});

	m_listener.subscribe<event::WorkspacePasteNode>([this](const auto& e) {
		if (m_handle.data() != Engine::get()->activeWorkspace().data()) {
			return false;
		}
		if (not s_clipboard) {
			return true;
		}
		auto par = findFrom(m_root_node, e.parent);
		if (not par.exists()) {
			TOAST_WARN("World", "WorkspacePasteNode: parent not found");
			return true;
		}

		assets::Handle<assets::Prefab> handle(s_clipboard.get(), toast::UID(0), "");
		InstantiateContext ctx;
		ctx.resolver = [](toast::UID id) { return assets::load<assets::Prefab>(id); };
		Box<Node> copy = instantiate(handle, ctx);
		if (not copy.exists()) {
			TOAST_WARN("World", "WorkspacePasteNode: instantiation failed");
			return true;
		}

		auto regen = [&](auto& self, Box<Node>& node) -> void {
			INodeOwner::generateUid(node);
			for (auto& child : node->m_children) {
				self(self, child);
			}
		};
		regen(regen, copy);

		copy->m_source_prefab = {};
		copy->m_name = uniqueChildName(*par, copy->name());
		copy->m_parent = par;
		par->m_children.emplace_back(copy);
		copy->m_state = par->m_state;
		copy->m_type = NodeType::child;
		copy->m_inherited_enabled = par->enabled();

		copy->propagateCallTick(copy->info(), TickFunctionList::init);
		copy->propagateCallTick(copy->info(), TickFunctionList::begin);
		copy->enabled(true);

		event::send<event::RequestHierarchyUpdate>();
		return true;
	});
}

void Workspace::tick() {
	// Only the active workspace streams inspector data, and only while a node is focused
	if (m_handle.data() != Engine::get()->activeWorkspace().data() || not m_focused_node.exists()) {
		m_inspector_accum = 0.0;
		return;
	}

	// Throttle to 12 fps instead of pushing the whole field set every frame
	m_inspector_accum += Time::delta();
	if (m_inspector_accum < 1.0 / 12.0) {
		return;
	}
	m_inspector_accum = 0.0;

	std::vector<event::InspectorContent::InspectorField> fields;
	Node* node = &*m_focused_node;

	for (const NodeInfo* type = node->info(); type != nullptr; type = type->base_type) {
		for (const auto& field : type->all_fields) {
			std::any value = field.get(node);

			std::string text;
			if (field.value_type == FieldType::quaternion_t && not field.is_array) {
				// rotation is exchanged with the inspector as euler degrees, not as a raw quaternion
				glm::vec3 deg = glm::degrees(glm::eulerAngles(std::any_cast<glm::quat>(value)));
				text = std::format("{} {} {}", deg.x, deg.y, deg.z);
			} else if (field.value_type == FieldType::uid_t && not field.is_array) {
				if (auto* box = std::any_cast<Box<Node>>(&value); box != nullptr) {
					text = box->exists() ? (*box)->uid().get() : "";
				} else if (auto* id = std::any_cast<UID>(&value); id != nullptr) {
					text = id->data() != 0 ? id->get() : "";
				}
			} else {
				try {
					text = assets::Prefab::stringifyValue(field.value_type, field.is_array, value);
				} catch (const std::bad_any_cast&) { text = ""; }
			}

			fields.emplace_back(field.name, text);
		}
	}

	event::send<event::InspectorContent>(
	    m_focused_node->uid().get(), m_focused_node->name(), m_focused_node->enabled(), std::move(fields)
	);

	// The exported script variables travel in their own message so the editor can rebuild
	// its Lua cards independently of the reflected C++ structure
	std::vector<event::InspectorLuaContent::LuaScriptCard> cards;
	uint32_t lua_schema_version = 0;

	if (scripting::ScriptRuntime* rt = node->scriptRuntime()) {
		lua_schema_version = rt->schemaVersion();
		cards.reserve(rt->instanceCount());

		for (size_t i = 0; i < rt->instanceCount(); ++i) {
			const scripting::ScriptSchema* schema = rt->instanceSchema(i);
			if (schema == nullptr) {
				continue;
			}

			auto make_field = [&](const LuaVarDesc& d) {
				event::InspectorLuaContent::LuaField f;
				f.path = std::format("{}:{}", i, d.path);
				f.name = d.name;
				f.kind = static_cast<uint32_t>(d.kind);
				f.is_array = d.is_array;
				f.ref_type = d.ref_type;
				f.value = stringifyLuaValue(d, rt->getVarByPath(i, d.path));
				f.default_value = stringifyLuaValue(d, d.default_value);
				return f;
			};

			event::InspectorLuaContent::LuaScriptCard card;
			card.script = rt->instanceScript(i);
			for (const auto& d : schema->fields) {
				card.fields.push_back(make_field(d));
			}
			for (const auto& g : schema->groups) {
				event::InspectorLuaContent::LuaGroup group;
				group.name = g.name;
				for (const auto& d : g.fields) {
					group.fields.push_back(make_field(d));
				}
				for (const auto& sg : g.subgroups) {
					event::InspectorLuaContent::LuaSubgroup sub;
					sub.name = sg.name;
					for (const auto& d : sg.fields) {
						sub.fields.push_back(make_field(d));
					}
					group.subgroups.push_back(std::move(sub));
				}
				card.groups.push_back(std::move(group));
			}
			cards.push_back(std::move(card));
		}
	}

	event::send<event::InspectorLuaContent>(m_focused_node->uid().get(), lua_schema_version, std::move(cards));
}

auto Workspace::gizmoRenderState() const -> GizmoRenderState {
	GizmoRenderState state;
	const bool tool_has_gizmo =
	    m_gizmo_tool == GizmoTool::translate || m_gizmo_tool == GizmoTool::rotate || m_gizmo_tool == GizmoTool::scale;
	state.visible = not isPlaying() && tool_has_gizmo && m_focused_node.as<Node3D>().exists();
	if (not state.visible) {
		return state;
	}
	state.tool = m_gizmo_tool;
	state.origin = gizmoOrigin();
	state.orientation = gizmoOrientation();
	state.hover = m_gizmo_hover;
	state.active = m_gizmo_drag;
	state.drag_scale_factor = m_gizmo_drag_current_factor;
	return state;
}

}
