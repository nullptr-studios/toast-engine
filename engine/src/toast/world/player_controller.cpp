#include "player_controller.hpp"

#include <toast/input/action.hpp>
#include <toast/input/haptics_system.hpp>
#include <toast/input/input_events.hpp>
#include <toast/log.hpp>
#include <typeinfo>

namespace input {

void PlayerController::init() {
	active_layout = default_layout;
	active_layer = default_layer.empty() ? std::string(assets::InputLayout::default_layer) : default_layer;

	rebuildEnabledActions();

	// Storing the parent registers a tick dependency so we run after it is ready
	m_parent = parent();

	listener().subscribe<event::InputEvent>([this](const event::InputEvent& e) {
		if (m_enabled_actions.contains(e.action_id.data())) {
			dispatchToParent(e);
		}
		return false;
	});

	listener().subscribe<event::SetInputLayout>([this](const event::SetInputLayout& e) {
		if (matchesTarget(e.target)) {
			setLayout(e.layout);
		}
		return false;
	});

	listener().subscribe<event::SetInputLayer>([this](const event::SetInputLayer& e) {
		if (matchesTarget(e.target)) {
			setLayer(e.layer);
		}
		return false;
	});

	listener().subscribe<event::PlayHaptic>([this](const event::PlayHaptic& e) {
		if (matchesTarget(e.target)) {
			playHaptic(e.haptic);
		}
		return false;
	});

	TOAST_TRACE("Input", "PlayerController init: layout '{}', layer '{}'", active_layout, active_layer);
}

void PlayerController::setLayout(std::string_view layout) {
	active_layout = std::string(layout);
	rebuildEnabledActions();
}

void PlayerController::setLayer(std::string_view layer) {
	active_layer = std::string(layer);
	rebuildEnabledActions();
}

void PlayerController::playHaptic(assets::AssetHandle<assets::Haptic> haptic) {
	HapticsSystem::get().play(controller_id, std::move(haptic));
}

void PlayerController::rebuildEnabledActions() {
	m_enabled_actions.clear();
	for (const auto& handle : layouts) {
		if (!handle.hasValue() || handle->name() != active_layout) {
			continue;
		}
		for (const auto& entry : handle->entries()) {
			if (assets::InputLayout::isActiveForLayer(entry, active_layer)) {
				m_enabled_actions.insert(entry.id.data());
			}
		}
	}
	TOAST_TRACE("Input", "PlayerController enabled {} action(s)", m_enabled_actions.size());
}

void PlayerController::dispatchToParent(const event::InputEvent& event) {
	if (!m_parent.exists()) {
		return;
	}

	toast::Node& target = *m_parent;
	const toast::NodeInfo* info = target.info();
	if (info == nullptr) {
		return;
	}

	const Action& action = event.action;
	const std::string function_name(action.functionName());
	const toast::FunctionInfo* method = info->getMethod(function_name);
	if (method == nullptr) {
		// The parent does not implement this action's function; nothing to call
		TOAST_WARN("Input", "Function '{}' does not exist on parent", function_name);
		return;
	}

	// Handlers take no argument or a single const input::Action&
	if (method->parameters.empty()) {
		info->call<void>(&target, function_name);
		return;
	}

	const bool takes_action = method->parameters.size() == 1 && method->parameters[0].type_id != nullptr &&
	                          *method->parameters[0].type_id == typeid(input::Action);
	if (takes_action) {
		info->call<void>(&target, function_name, action);
		return;
	}

	TOAST_WARN(
	    "Input",
	    "Function '{}' on parent has an incompatible signature; expected (const input::Action&) or no arguments",
	    function_name
	);
}

auto PlayerController::matchesTarget(std::string_view target) const -> bool {
	if (!m_parent.exists()) {
		return false;
	}
	const toast::Node& parent_node = *m_parent;
	return target == parent_node.name() || target == parent_node.uid().get();
}

}
