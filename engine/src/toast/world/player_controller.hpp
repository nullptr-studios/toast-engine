/**
 * @file player_controller.hpp
 * @author Xein
 * @date 21 Jun 2026
 *
 * @brief Controller node driven by a human player
 */

#pragma once

#include "box.hpp"
#include "icontroller.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <toast/assets/types.hpp>
#include <unordered_set>
#include <vector>

namespace event {
struct InputEvent;
struct SetInputLayout;
struct SetInputLayer;
struct PlayHaptic;
}

namespace input {

/**
 * @brief Controller driven by a human player
 */
class [[ToastNode]] TOAST_API PlayerController : public input::IController {
public:
	/**
	 * @brief Switches the active layout by name and recomputes the enabled action set
	 * @param layout Name of a layout owned by this controller
	 */
	void setLayout(std::string_view layout);

	/**
	 * @brief Switches the active layer by name and recomputes the enabled action set
	 * @param layer Name of a layer present in the active layout
	 */
	void setLayer(std::string_view layer);

	/**
	 * @brief Plays a haptic effect on this controller's physical gamepad
	 * @param haptic The effect to play, routed through @c controller_id
	 */
	void playHaptic(assets::Handle<assets::Haptic> haptic) const;

	[[Reflect, ReadOnly]]
	std::string active_layout;

	[[Reflect, ReadOnly]]
	std::string active_layer;

	[[Reflect]]
	std::string default_layout;

	[[Reflect]]
	std::string default_layer;

	[[Reflect]]
	std::vector<assets::Handle<assets::InputLayout>> layouts;

	[[Reflect]]
	bool use_settings = false;

	[[Reflect]]
	std::string settings_name;

	/// SDL_JoystickID of the controller this player drives; 0 = first/active gamepad
	[[Reflect]]
	uint32_t controller_id = 0;

private:
	void init();

	void rebuildEnabledActions();

	void dispatchToParent(const event::InputEvent& event);

	[[nodiscard]]
	auto matchesTarget(std::string_view target) const -> bool;

	std::unordered_set<uint64_t> m_enabled_actions;
	toast::Box<toast::Node> m_parent;
};

}
