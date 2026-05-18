#include "node.hpp"

namespace toast {

#pragma region FUNCTION_TABLE_ITERATIONS

void NodeFunctionTable::load(Node* n) {
	for (auto& f : table.load) {
		f(n);
	}
}

void NodeFunctionTable::save(Node* n) {
	for (auto& f : table.save) {
		f(n);
	}
}

void NodeFunctionTable::preInit(Node* n) {
	for (auto& f : table.pre_init) {
		f(n);
	}
}

void NodeFunctionTable::init(Node* n) {
	for (auto& f : table.init) {
		f(n);
	}
}

void NodeFunctionTable::begin(Node* n) {
	for (auto& f : table.begin) {
		f(n);
	}
}

void NodeFunctionTable::onEnable(Node* n) {
	for (auto& f : table.on_enable) {
		f(n);
	}
}

void NodeFunctionTable::earlyTick(Node* n) {
	for (auto& f : table.early_tick) {
		f(n);
	}
}

void NodeFunctionTable::tick(Node* n) {
	for (auto& f : table.tick) {
		f(n);
	}
}

void NodeFunctionTable::postPhysics(Node* n) {
	for (auto& f : table.post_physics) {
		f(n);
	}
}

void NodeFunctionTable::lateTick(Node* n) {
	for (auto& f : table.late_tick) {
		f(n);
	}
}

void NodeFunctionTable::onDisable(Node* n) {
	for (auto& f : table.on_disable) {
		f(n);
	}
}

void NodeFunctionTable::end(Node* n) {
	for (auto& f : table.end) {
		f(n);
	}
}

void NodeFunctionTable::destroy(Node* n) {
	for (auto& f : table.destroy) {
		f(n);
	}
}

#pragma endregion FUNCTION_TABLE_ITERATIONS

auto Node::uuid() const noexcept -> UUID {
	return m.uuid;
}

auto Node::name() const noexcept -> std::string {
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
		table->onEnable(this);
	} else {
		table->onDisable(this);
	}

	for (auto& c : m.children) {
		c.inheritedEnabled(value);
	}
}

auto Node::parent() const noexcept -> Node* {
	return m.parent;
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
			table->onEnable(this);
		} else {
			table->onDisable(this);
		}
	}

	for (auto& c : m.children) {
		c.inheritedEnabled(value);
	}
}

}
