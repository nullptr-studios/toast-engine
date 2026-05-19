#include "node.hpp"

#include "world.hpp"

namespace toast {

#pragma region FUNCTION_TABLE_ITERATIONS

void NodeFunctionTable::load(Node& n) {
	ZoneScoped;
	ZoneNameF("%s::load()", n.name().data());

	for (auto& f : table.load) {
		ZoneScoped;
		f(&n);
	}
}

void NodeFunctionTable::save(Node& n) {
	ZoneScoped;
	ZoneNameF("%s::save()", n.name().data());

	for (auto& f : table.save) {
		ZoneScoped;
		f(&n);
	}
}

void NodeFunctionTable::preInit(Node& n) {
	ZoneScoped;
	ZoneNameF("%s::preInit()", n.name().data());

	for (auto& f : table.pre_init) {
		ZoneScoped;
		f(&n);
	}
}

void NodeFunctionTable::init(Node& n) {
	ZoneScoped;
	ZoneNameF("%s::init()", n.name().data());

	for (auto& f : table.init) {
		ZoneScoped;
		f(&n);
	}
}

void NodeFunctionTable::begin(Node& n) {
	ZoneScoped;
	ZoneNameF("%s::begin()", n.name().data());

	for (auto& f : table.begin) {
		ZoneScoped;
		f(&n);
	}
}

void NodeFunctionTable::onEnable(Node& n) {
	ZoneScoped;
	ZoneNameF("%s::onEnable()", n.name().data());

	for (auto& f : table.on_enable) {
		ZoneScoped;
		f(&n);
	}
}

void NodeFunctionTable::earlyTick(Node& n) {
	ZoneScoped;
	ZoneNameF("%s::earlyTick()", n.name().data());

	for (auto& f : table.early_tick) {
		ZoneScoped;
		f(&n);
	}
}

void NodeFunctionTable::tick(Node& n) {
	ZoneScoped;
	ZoneNameF("%s::tick()", n.name().data());

	for (auto& f : table.tick) {
		ZoneScoped;
		f(&n);
	}
}

void NodeFunctionTable::postPhysics(Node& n) {
	ZoneScoped;
	ZoneNameF("%s::postPhysics()", n.name().data());

	for (auto& f : table.post_physics) {
		ZoneScoped;
		f(&n);
	}
}

void NodeFunctionTable::lateTick(Node& n) {
	ZoneScoped;
	ZoneNameF("%s::lateTick()", n.name().data());

	for (auto& f : table.late_tick) {
		ZoneScoped;
		f(&n);
	}
}

void NodeFunctionTable::onDisable(Node& n) {
	ZoneScoped;
	ZoneNameF("%s::onDisable()", n.name().data());

	for (auto& f : table.on_disable) {
		ZoneScoped;
		f(&n);
	}
}

void NodeFunctionTable::end(Node& n) {
	ZoneScoped;
	ZoneNameF("%s::end()", n.name().data());

	for (auto& f : table.end) {
		ZoneScoped;
		f(&n);
	}
}

void NodeFunctionTable::destroy(Node& n) {
	ZoneScoped;
	ZoneNameF("%s::destroy()", n.name().data());

	for (auto& f : table.destroy) {
		ZoneScoped;
		f(&n);
	}
}

// btw dante i hate you this was a horrible experience

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

}
