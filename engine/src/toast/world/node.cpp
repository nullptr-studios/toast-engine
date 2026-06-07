#include "node.hpp"

#include "world.hpp"

namespace toast {

auto Node::uuid() const noexcept -> const UUID& {
	return m_uuid;
}

auto Node::name() const noexcept -> std::string_view {
	return m_name;
}

void Node::name(std::string_view name) noexcept {
	m_name = name;
}

auto Node::enabled() const noexcept -> bool {
	return m_local_enabled && m_inherited_enabled;
}

void Node::enabled(bool value) noexcept {
	if (m_local_enabled == value) {
		return;
	}
	if (not m_inherited_enabled) {
		return;
	}
	m_local_enabled = value;

	if (value) {
		callTick(m_info, TickFunctionList::on_enable);
	} else {
		callTick(m_info, TickFunctionList::on_disable);
	}

	for (auto& c : m_children) {
		c->inheritedEnabled(value);
	}
}

auto Node::box() const noexcept -> Box<Node> {
	return m_box;
}

auto Node::parent() noexcept -> Box<Node> {
	World::registerDependency(m_parent, *this);
	return m_parent;
}

auto Node::addChild() -> Box<Node> {
	return World::requestRuntimeCreation(*this);
}

auto Node::info() const -> const NodeInfo* {
	return m_info;
}

auto Node::listener() noexcept -> event::Listener& {
	if (not m_listener) {
		m_listener = std::make_unique<event::Listener>();
	}

	return *m_listener;
}

void Node::inheritedEnabled(bool value) noexcept {
	if (m_inherited_enabled == value) {
		return;
	}
	m_inherited_enabled = value;

	if (m_local_enabled) {
		if (value) {
			callTick(m_info, TickFunctionList::on_enable);
		} else {
			callTick(m_info, TickFunctionList::on_disable);
		}
	}

	for (auto& c : m_children) {
		c->inheritedEnabled(value);
	}
}

void Node::changeNodeState(NodeState state) noexcept {
	m_state = state;
	for (auto& c : m_children) {
		c->m_state = state;
	}
}

void Node::callTick(const NodeInfo* info, TickFunctionList func_type) noexcept {
	if (!info) {
		return;
	}

	// Frame-tick functions only run on enabled nodes; lifecycle/enable callbacks always run.
	if (has_flag(TickFunctionList::tick_mask, func_type) && not enabled()) {
		return;
	}

	// Walk base → derived
	if (info->base_type) {
		callTick(info->base_type, func_type);
	}

	// Call this level's function
	const TickFunctions& funcs = info->functions;
	TickFunctions::Invoker invoker = nullptr;

	if (has_flag(func_type, TickFunctionList::load) && has_flag(funcs.list, TickFunctionList::load)) {
		invoker = funcs.load;
	} else if (has_flag(func_type, TickFunctionList::save) && has_flag(funcs.list, TickFunctionList::save)) {
		invoker = funcs.save;
	} else if (has_flag(func_type, TickFunctionList::pre_init) && has_flag(funcs.list, TickFunctionList::pre_init)) {
		invoker = funcs.pre_init;
	} else if (has_flag(func_type, TickFunctionList::init) && has_flag(funcs.list, TickFunctionList::init)) {
		invoker = funcs.init;
	} else if (has_flag(func_type, TickFunctionList::destroy) && has_flag(funcs.list, TickFunctionList::destroy)) {
		invoker = funcs.destroy;
	} else if (has_flag(func_type, TickFunctionList::begin) && has_flag(funcs.list, TickFunctionList::begin)) {
		invoker = funcs.begin;
	} else if (has_flag(func_type, TickFunctionList::end) && has_flag(funcs.list, TickFunctionList::end)) {
		invoker = funcs.end;
	} else if (has_flag(func_type, TickFunctionList::on_enable) && has_flag(funcs.list, TickFunctionList::on_enable)) {
		invoker = funcs.on_enable;
	} else if (has_flag(func_type, TickFunctionList::on_disable) && has_flag(funcs.list, TickFunctionList::on_disable)) {
		invoker = funcs.on_disable;
	} else if (has_flag(func_type, TickFunctionList::early_tick) && has_flag(funcs.list, TickFunctionList::early_tick)) {
		invoker = funcs.early_tick;
	} else if (has_flag(func_type, TickFunctionList::tick) && has_flag(funcs.list, TickFunctionList::tick)) {
		invoker = funcs.tick;
	} else if (has_flag(func_type, TickFunctionList::post_physics) && has_flag(funcs.list, TickFunctionList::post_physics)) {
		invoker = funcs.post_physics;
	} else if (has_flag(func_type, TickFunctionList::late_tick) && has_flag(funcs.list, TickFunctionList::late_tick)) {
		invoker = funcs.late_tick;
	}

	if (invoker) {
		invoker(this);
	}
}

void Node::propagateCallTick(const NodeInfo* info, TickFunctionList func_type) noexcept {
	callTick(info, func_type);

	for (auto& child : m_children) {
		child->propagateCallTick(info, func_type);
	}
}

}
