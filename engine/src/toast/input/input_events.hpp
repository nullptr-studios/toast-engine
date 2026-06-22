/**
 * @file input_events.hpp
 * @author Xein
 * @date 21 Jun 2026
 */

#pragma once

#include <cstdint>
#include <string>
#include <toast/assets/core_types.hpp>
#include <toast/events/event.hpp>
#include <toast/uid.hpp>

namespace input {
class Action;
enum class ActionEvent : uint8_t;
enum class Device : uint8_t;
}

namespace assets {
class Haptic;
}

namespace event {

/**
 * @brief Sent whenever an input action fires
 */
struct InputEvent : Event<InputEvent> {
	InputEvent(toast::UID action_id, input::Action& action, input::ActionEvent type)
	    : action_id(action_id),
	      action(action),
	      type(type) { }

	toast::UID action_id;
	input::Action& action;
	input::ActionEvent type;
};

/**
 * @brief Sent when the most recently used input device changes
 */
struct LastInputType : Event<LastInputType> {
	LastInputType(input::Device device, std::string name) : device(device), name(std::move(name)) { }

	input::Device device;
	std::string name;
};

/**
 * @brief Requests that a controller switch its active layout
 */
struct SetInputLayout : Event<SetInputLayout> {
	SetInputLayout(std::string target, std::string layout) : target(std::move(target)), layout(std::move(layout)) { }

	std::string target;
	std::string layout;
};

/**
 * @brief Requests that a controller switch its active layer
 */
struct SetInputLayer : Event<SetInputLayer> {
	SetInputLayer(std::string target, std::string layer) : target(std::move(target)), layer(std::move(layer)) { }

	std::string target;
	std::string layer;
};

/**
 * @brief Requests that the input system rebuild its actions from the asset manifest
 */
struct ReloadInputActions : Event<ReloadInputActions> { };

/**
 * @brief Requests that a controller play a haptic effect
 */
struct PlayHaptic : Event<PlayHaptic> {
	/// @param target Name of the PlayerController's parent
	PlayHaptic(std::string target, assets::AssetHandle<assets::Haptic> haptic)
	    : target(std::move(target)),
	      haptic(std::move(haptic)) { }

	std::string target;
	assets::AssetHandle<assets::Haptic> haptic;
};

/**
 * @brief Sets the global haptics intensity multiplier
 */
struct SetHapticsMultiplier : Event<SetHapticsMultiplier> {
	explicit SetHapticsMultiplier(float multiplier) : multiplier(multiplier) { }

	float multiplier;
};

}
