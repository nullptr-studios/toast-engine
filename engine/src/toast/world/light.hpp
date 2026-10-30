/**
 * @file light.hpp
 * @author Xein
 * @date 22 Jun 2026
 *
 * @brief Base light node; registers with the renderer so it can be collected into the per-frame light list
 */

#pragma once
#include "node_3d.hpp"

#include <algorithm>
#include <toast/export.hpp>

namespace toast {

/// @brief Concrete light kind, tagged once by each subclass's constructor - not RTTI, not reflected
enum class LightType : uint8_t {
	none,
	point,
	directional,
	spot,
	ambient,
};

/// Mirrors lighting.slang kLightShape
enum class LightShape : uint8_t {
	point,
	sphere,
	/// Along the node forward axis
	capsule,
	/// Facing the node forward axis
	disc,
	/// Facing forward with its width along the node right axis
	rect,
};

/// Mirrors lighting.slang kShadowMode and only affects voxel pixels
enum class LightShadowMode : uint8_t {
	shadow_map,
	/// One marched ray to a point on the shape per pixel
	marched,
	/// Shares one marched ray with every sampled light of the froxel
	sampled,
};

class [[ToastNode, Hidden, Icon("PointLight")]] TOAST_API Light : public Node3D {
public:
	Light() = default;

	[[nodiscard]]
	auto lightType() const -> LightType {
		return m_light_type;
	}

	[[nodiscard]]
	auto color() const -> const glm::vec3& {
		return m_light_color;
	}

	[[nodiscard]]
	auto intensity() const -> float {
		return m_intensity;
	}

	/// @brief Whether this light should be rendered into a shadow map
	[[nodiscard]]
	auto castsShadows() const -> bool {
		return m_cast_shadows;
	}

	/// @brief Multiplier on the shadow-map resolution this light is granted, see m_shadow_resolution_scale
	[[nodiscard]]
	auto shadowResolutionScale() const -> float {
		return m_shadow_resolution_scale;
	}

	/// @brief Camera distance past which this light stops casting shadows; 0 disables the limit
	[[nodiscard]]
	auto shadowDistance() const -> float {
		return m_shadow_distance;
	}

	/// @brief Camera distance past which this light stops being submitted at all; 0 disables the limit
	[[nodiscard]]
	auto cullDistance() const -> float {
		return m_cull_distance;
	}

	[[nodiscard]]
	auto shape() const -> LightShape {
		return m_shape;
	}

	[[nodiscard]]
	auto sourceRadius() const -> float {
		return m_source_radius;
	}

	[[nodiscard]]
	auto sourceLength() const -> float {
		return m_source_length;
	}

	[[nodiscard]]
	auto sourceWidth() const -> float {
		return m_source_width;
	}

	[[nodiscard]]
	auto shadowMode() const -> LightShadowMode {
		return m_shadow_mode;
	}

	void setSourceRadius(float radius) { m_source_radius = std::max(radius, 0.0f); }

	void setSourceLength(float length) { m_source_length = std::max(length, 0.0f); }

	void setSourceWidth(float width) { m_source_width = std::max(width, 0.0f); }

protected:
	/// @brief Tags the concrete light kind; called once from each subclass's own constructor body
	void setLightType(LightType type) { m_light_type = type; }

private:
	void init();
	void end();
	void destroy();

	[[Reflect, Color]]
	glm::vec3 m_light_color = glm::vec3(1.0f, 1.0f, 1.0f);

	[[Reflect, Unit("lm")]]
	float m_intensity = 1.0f;

	[[Reflect, Name("Cast Shadows"), Group("Optimization")]]
	bool m_cast_shadows = true;

	/// Scales the resolution this light's shadow map is rendered at, on top of the automatic
	/// distance-based reduction (see renderer::shadows::punctualShadowResolution). Turn it down on a light
	/// whose shadow is soft or rarely looked at, up on the one the player is standing next to.
	/// PointLight/Spotlight only - the directional cascades have an image to themselves
	[[Reflect, Name("Shadow Resolution Scale"), Range(0.1, 1.0), Group("Optimization")]]
	float m_shadow_resolution_scale = 1.0f;

	/// Camera-to-light distance past which this light stops casting shadows, though it still lights the
	/// scene. Shadow slots are scarce (renderer.shadows.spot_slots / point_slots) and
	/// this is what hands one back to a nearer light. 0 means no limit
	[[Reflect, Name("Shadow Distance"), Unit("m"), Group("Optimization")]]
	float m_shadow_distance = 60.0f;

	/// Camera-to-light distance past which this light is dropped from the frame entirely - no shading, no
	/// shadow, not even a cluster-culling test. 0 means never culled, which is the safe default: a light
	/// popping out is far more visible than one costing a few cycles
	[[Reflect, Name("Cull Distance"), Unit("m"), Group("Optimization")]]
	float m_cull_distance = 0.0f;

	[[Reflect, Name("Shape"), Enum("Point", "Sphere", "Capsule", "Disc", "Rect"), Group("Area")]]
	LightShape m_shape = LightShape::point;

	/// Sphere capsule and disc radius
	[[Reflect, Name("Source Radius"), Unit("m"), Range(0.0, 10.0), Group("Area")]]
	float m_source_radius = 0.1f;

	/// Capsule length or rect height
	[[Reflect, Name("Source Length"), Unit("m"), Range(0.0, 20.0), Group("Area")]]
	float m_source_length = 0.5f;

	[[Reflect, Name("Source Width"), Unit("m"), Range(0.0, 20.0), Group("Area")]]
	float m_source_width = 0.5f;

	/// Marched and sampled soften voxel shadows by the shape but meshes stop shadowing voxels for this light
	[[Reflect, Name("Voxel Shadows"), Enum("Shadow Map", "Marched", "Sampled"), Group("Area")]]
	LightShadowMode m_shadow_mode = LightShadowMode::shadow_map;

	// Deliberately not [[Reflect]] - applyFields() only ever touches reflected fields during prefab
	// deserialization, so this constructor-set value can never be clobbered
	LightType m_light_type = LightType::none;

	bool m_registered_proxy = false;
};
}
