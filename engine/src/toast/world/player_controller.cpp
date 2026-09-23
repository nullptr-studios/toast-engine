#include "player_controller.hpp"

#include <algorithm>
#include <toast/input/action.hpp>
#include <toast/input/haptics_system.hpp>
#include <toast/input/input_events.hpp>
#include <toast/log.hpp>
#include <toast/window/window_events.hpp>
#include <typeinfo>

namespace input {

namespace {
bool g_mouse_locked = false;
}

void PlayerController::updateInspectorMessages() {
	static const toast::NodeMessage parent_message {
	  .severity = toast::NodeMessage::error,
	  .id = 21,
	  .text = "PlayerController must be a child of a Node3D",
	};
	static const toast::NodeMessage layout_message {
	  .severity = toast::NodeMessage::warning,
	  .id = 22,
	  .text = "PlayerController requires one valid layout",
	};

	if (parent().exists()) {
		removeInspectorMessage(parent_message);
	} else {
		addInspectorMessage(parent_message);
	}
	const bool has_layout = std::ranges::any_of(layouts, [](const auto& layout) { return layout.hasValue(); });
	if (has_layout) {
		removeInspectorMessage(layout_message);
	} else {
		addInspectorMessage(layout_message);
	}
}

void PlayerController::init() {
	active_layout = default_layout;
	active_layer = default_layer.empty() ? std::string(assets::InputLayout::default_layer) : default_layer;

	rebuildEnabledActions();

	// Storing the parent registers a tick dependency so we run after it is ready
	m_parent = parent();

	listener().subscribe<event::InputEvent>([this](const event::InputEvent& e) {
		if (!participatesIn(toast::NodeOwnerParticipation::runtime_input)) {
			return false;
		}
		if (m_enabled_actions.contains(e.action_id.data())) {
			dispatchToParent(e);
		}
		return false;
	});

	listener().subscribe<event::SetInputLayout>([this](const event::SetInputLayout& e) {
		if (!participatesIn(toast::NodeOwnerParticipation::runtime_input)) {
			return false;
		}
		if (matchesTarget(e.target)) {
			setLayout(e.layout);
		}
		return false;
	});

	listener().subscribe<event::SetInputLayer>([this](const event::SetInputLayer& e) {
		if (!participatesIn(toast::NodeOwnerParticipation::runtime_input)) {
			return false;
		}
		if (matchesTarget(e.target)) {
			setLayer(e.layer);
		}
		return false;
	});

	listener().subscribe<event::PlayHaptic>([this](const event::PlayHaptic& e) {
		if (!participatesIn(toast::NodeOwnerParticipation::runtime_input)) {
			return false;
		}
		if (matchesTarget(e.target)) {
			playHaptic(e.haptic);
		}
		return false;
	});

	TOAST_TRACE("Input", "PlayerController init: layout '{}', layer '{}'", active_layout, active_layer);
}

void PlayerController::end() {
	if (m_holds_mouse_lock) {
		setMouseLocked(false);
	}
}

void PlayerController::setLayout(std::string_view layout) {
	active_layout = std::string(layout);
	rebuildEnabledActions();
}

void PlayerController::setLayer(std::string_view layer) {
	active_layer = std::string(layer);
	rebuildEnabledActions();
}

void PlayerController::playHaptic(assets::Handle<assets::Haptic> haptic) const {
	HapticsSystem::get().play(controller_id, std::move(haptic));
}

void PlayerController::setMouseLocked(bool locked) {
	if (locked && !participatesIn(toast::NodeOwnerParticipation::runtime_input)) {
		return;
	}
	m_holds_mouse_lock = locked;
	if (g_mouse_locked == locked) {
		return;
	}
	g_mouse_locked = locked;
	event::send<event::WindowMouseLock>(locked);
}

auto PlayerController::isMouseLocked() const -> bool {
	return g_mouse_locked;
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
	const Action& action = event.action;
	const std::string function_name(action.functionName());
	const toast::FunctionInfo* method = info != nullptr ? info->getMethod(function_name) : nullptr;
	if (method == nullptr) {
		if (!target.hasCallable(function_name)) {
			TOAST_WARN("Input", "Function '{}' does not exist on parent", function_name);
			return;
		}

		target.call<void>(function_name, action, event.type);
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
		target.call<void>(function_name, action);
		return;
	}

	const bool takes_action_event = method->parameters.size() == 2 && method->parameters[0].type_id != nullptr &&
	                                *method->parameters[0].type_id == typeid(input::Action) &&
	                                method->parameters[1].type_id != nullptr &&
	                                *method->parameters[1].type_id == typeid(input::ActionEvent);
	if (takes_action_event) {
		target.call<void>(function_name, action, event.type);
		return;
	}

	TOAST_WARN(
	    "Input",
	    "Function '{}' on parent has an incompatible signature; expected (), (const input::Action&), or "
	    "(const input::Action&, input::ActionEvent)",
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
