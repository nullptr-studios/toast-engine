#include "node.hpp"

#include "world.hpp"

namespace toast {

#pragma region FUNCTION_TABLE_ITERATIONS

#define TOAST_NODE_FUNCTION_IMPL(fn_name, member_name) \
	void NodeFunctionTable::fn_name(Node& n) {           \
		ZoneScoped;                                        \
		ZoneNameF("%s::" #fn_name "()", n.name().data());  \
		if (not n.enabled())                               \
			return;                                          \
		for (auto& f : table.member_name) {                \
			ZoneScoped;                                      \
			f(&n);                                           \
		}                                                  \
	}

#define TOAST_NODE_PROPAGATE_IMPL(fn_name)              \
	void NodeFunctionTable::fn_name##Propagate(Node& n) { \
		ZoneScoped;                                         \
		fn_name(n);                                         \
		for (auto& c : n.m.children) {                      \
			c->table->fn_name##Propagate(c);                  \
		}                                                   \
	}

#define TOAST_NODE_HAS_IMPL(fn_name, member_name)       \
	auto NodeFunctionTable::has##fn_name(Node& n)->bool { \
		return not n.table->table.member_name.empty();      \
	}

TOAST_NODE_FUNCTION_IMPL(load, load)
TOAST_NODE_FUNCTION_IMPL(save, save)
TOAST_NODE_FUNCTION_IMPL(preInit, pre_init)
TOAST_NODE_FUNCTION_IMPL(init, init)
TOAST_NODE_FUNCTION_IMPL(begin, begin)
TOAST_NODE_FUNCTION_IMPL(onEnable, on_enable)
TOAST_NODE_FUNCTION_IMPL(earlyTick, early_tick)
TOAST_NODE_FUNCTION_IMPL(tick, tick)
TOAST_NODE_FUNCTION_IMPL(postPhysics, post_physics)
TOAST_NODE_FUNCTION_IMPL(lateTick, late_tick)
TOAST_NODE_FUNCTION_IMPL(onDisable, on_disable)
TOAST_NODE_FUNCTION_IMPL(end, end)
TOAST_NODE_FUNCTION_IMPL(destroy, destroy)

TOAST_NODE_PROPAGATE_IMPL(load)
TOAST_NODE_PROPAGATE_IMPL(save)
TOAST_NODE_PROPAGATE_IMPL(preInit)
TOAST_NODE_PROPAGATE_IMPL(init)
TOAST_NODE_PROPAGATE_IMPL(begin)
TOAST_NODE_PROPAGATE_IMPL(end)
TOAST_NODE_PROPAGATE_IMPL(destroy)

TOAST_NODE_HAS_IMPL(EarlyTick, early_tick)
TOAST_NODE_HAS_IMPL(Tick, tick)
TOAST_NODE_HAS_IMPL(PostPhysics, post_physics)
TOAST_NODE_HAS_IMPL(LateTick, late_tick)

#undef TOAST_NODE_FUNCTION_IMPL
#undef TOAST_NODE_PROPAGATE_IMPL
#undef TOAST_NODE_HAS_IMPL

#pragma endregion FUNCTION_TABLE_ITERATIONS

auto Node::uuid() const noexcept -> const UUID& {
	return m.uuid;
}

auto Node::name() const noexcept -> std::string_view {
	return m.name;
}

void Node::name(std::string_view name) noexcept {
	m.name = name;
}

auto Node::enabled() const noexcept -> bool {
	return m.local_enabled && m.inherited_enabled;
}

void Node::enabled(bool value) noexcept {
	if (m.local_enabled == value) {
		return;
	}
	if (not m.inherited_enabled) {
		return;
	}
	m.local_enabled = value;

	if (value) {
		table->onEnable(*this);
	} else {
		table->onDisable(*this);
	}

	for (auto& c : m.children) {
		c->inheritedEnabled(value);
	}
}

auto Node::box() const noexcept -> Box<Node> {
	return m.box;
}

auto Node::parent() noexcept -> Box<Node> {
	World::registerDependency(m.parent, *this);
	return m.parent;
}

auto Node::addChild() -> Box<Node> {
	return World::requestRuntimeCreation(*this);
}

auto Node::listener() noexcept -> event::Listener& {
	if (not m.listener) {
		m.listener = std::make_unique<event::Listener>();
	}

	return *m.listener;
}

void Node::inheritedEnabled(bool value) noexcept {
	if (m.inherited_enabled == value) {
		return;
	}
	m.inherited_enabled = value;

	if (m.local_enabled) {
		if (value) {
			table->onEnable(*this);
		} else {
			table->onDisable(*this);
		}
	}

	for (auto& c : m.children) {
		c->inheritedEnabled(value);
	}
}

void Node::changeNodeState(NodeState state) noexcept {
	m.state = state;
	for (auto& c : m.children) {
		c->m.state = state;
	}
}

}
