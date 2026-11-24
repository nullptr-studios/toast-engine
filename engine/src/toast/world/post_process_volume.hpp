/**
 * @file post_process_volume.hpp
 * @author dario
 * @date 19/08/2026
 *
 * @brief Per-level post-process grading, as a node an artist places in the scene
 */

#pragma once
#include "volume.hpp"

#include <toast/export.hpp>
#include <toast/renderer/settings/post_process_params.hpp>
#include <toast/renderer/settings/post_process_profile.hpp>

namespace toast {

/**
 * @brief A region of the world that grades the post chain while the camera is inside it
 *
 * Post-process values are scene content, not machine settings. The right exposure in a cave is the wrong one
 * on a hilltop, and neither is a property of the hardware - so they live here, per level, rather than in the
 * settings registry beside shadow resolution and frame rate
 *
 * A volume is either **global**, applying everywhere as the level's base grade, or a **box** that takes over
 * as the camera enters it. Several may overlap: the renderer sorts them by priority and blends each one in
 * turn, so a level typically has one global volume for its overall look and small boxes for the rooms that
 * differ from it
 *
 * @note The blend is a crossfade, not a switch. A box fades in over blendDistance() as the camera approaches,
 *       because grading that snaps at a boundary reads as a bug even when the two grades are both correct
 */
class [[ToastNode, Icon("WorldEnvironment")]] TOAST_API PostProcessVolume : public Volume {
public:
	/// Volume defaults blendDistance to 0, which makes a grade snap at the boundary. A post volume is a
	/// crossfade, so it starts with a metre of blend
	PostProcessVolume() { blendDistance(1.0f); }

	~PostProcessVolume() override = default;

	/// @returns half-extents of the box, meaningless when global
	[[nodiscard]]
	auto extents() const noexcept -> const glm::vec3& {
		return m_extents;
	}

	[[nodiscard]]
	auto isEnabled() const noexcept -> bool {
		return m_enabled;
	}

	/// @returns the grade this volume contributes, at full weight
	[[nodiscard]]
	auto params() const -> const renderer::PostProcessParams&;

	/// @returns false when the name is unknown or not a scalar
	[[Reflect]]
	auto setParam(std::string_view name, float value) -> bool;

	[[Reflect]]
	auto setParamColor(std::string_view name, glm::vec3 value) -> bool;

	/// Sets an enumeration by option name, like setParamOption("tonemap.mode", "ACES")
	/// @returns false when the name is unknown, not an enumeration, or the option does not exist
	[[Reflect]]
	auto setParamOption(std::string_view name, std::string_view option) -> bool;

	/// @returns 0 when the name is unknown, see hasParam()
	[[Reflect]]
	auto getParam(std::string_view name) const -> float;

	/// @returns black when the name is unknown, see hasParam()
	[[Reflect]]
	auto getParamColor(std::string_view name) const -> glm::vec3;

	[[Reflect]]
	auto hasParam(std::string_view name) const -> bool;

	[[Reflect]]
	void resetParams();

	/**
	 * @brief How strongly this volume applies with the camera at @p camera_position
	 *
	 * 1 inside the box, falling to 0 across blendDistance() outside it, and always 1 when unbound. Measured
	 * against the box in the volume's own space, so a rotated volume fades along its own axes rather than
	 * along the world's.
	 *
	 * @returns 0 when the volume is disabled or the camera is beyond its influence
	 */
	[[nodiscard]]
	auto influenceAt(const glm::vec3& camera_position) const -> float;

	/// @brief Stores influenceAt(target.position), for callers driving the Volume accumulator interface
	///
	/// The renderer does not: it grades against one camera per frame, so it queries influenceAt() directly
	auto evaluateTarget(const VolumeTarget& target, float weight = 1.0f) -> bool override;

	void resetAccumulators() override;

private:
	/// Half-extents rather than the node's scale, so a volume can be sized without scaling its transform
	[[nodiscard]]
	auto closestPointOnBounds(glm::vec3 point) const -> glm::vec3 override;

	void init();
	void end();
	void destroy();

	[[Reflect, Name("Enabled")]]
	bool m_enabled = true;

	/// Volume supplies Is Global, Priority, Weight and Blend Distance; only the box shape is ours
	[[Reflect, Name("Extents"), Unit("m"), Group("Shape")]] alignas(16) glm::vec3 m_extents = glm::vec3(10.0f);

	/// Empty grades with the schema defaults
	[[Reflect, Name("Profile")]]
	assets::Handle<assets::PostProcessProfile> m_profile;

	void syncParams() const;

	mutable renderer::PostProcessParams m_params;
	mutable const assets::PostProcessProfile* m_synced_profile = nullptr;
	mutable uint32_t m_synced_version = 0;

	/// Last value evaluateTarget() computed; see the note there for why the renderer bypasses it
	float m_influence = 0.0f;

	bool m_registered_proxy = false;
};

}
