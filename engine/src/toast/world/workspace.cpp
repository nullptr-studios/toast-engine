#include "workspace.hpp"

#include "node.hpp"

#include <toast/assets/assets.hpp>
#include <toast/assets/prefab.hpp>
#include <toast/world/reflect.hpp>

namespace toast {

Workspace::Workspace(std::string_view type, std::string_view name) {
	eventSubscriptions();
}

Workspace::Workspace(std::string_view uri) {
	eventSubscriptions();
}

Workspace::Workspace(UID uid) {
	eventSubscriptions();
}

void Workspace::registerDependency(Node& from, Node& to) {
	// Do nothing?
}

auto Workspace::requestRuntimeCreation(Node& parent) -> Box<Node> {
	ZoneScoped;
	ZoneNameF("Workspace(%s)::requestRuntimeCreation(%s)", m_root_node->name().data(), parent.name().data());

	// Allocation
	Box node = this->nodeAllocation();
	node->propagateCallTick(node->info(), TickFunctionList::pre_init);

	// Data structure generation
	node->m_uid.generate();
	node->m_parent = parent;
	parent.m_children.emplace_back(node);
	node->m_state = parent.m_state;
	node->m_type = NodeType::child;
	node->m_inherited_enabled = parent.enabled();

	// Initialization
	node->callTick(node->info(), TickFunctionList::init);
	node->callTick(node->info(), TickFunctionList::begin);
	node->enabled(true);
	TOAST_TRACE("World", "Spawned node in {}", parent.name());
	return node;
}

auto Workspace::findFrom(const Node& origin, std::string_view query) -> Box<Node> { }

auto Workspace::searchFrom(const Node& origin, std::string_view query) -> std::vector<Box<Node>> { }

void Workspace::spawnInto(Node& parent, toast::UID prefab) {
	ZoneScoped;
	ZoneNameF("Workspace(%s)::spawnInto(%s)", m_root_node->name().data(), parent.name().data());

	auto file = assets::load<assets::Prefab>(prefab);
	if (not file.hasValue()) {
		TOAST_ERROR("World", "Couldn't load prefab {} to spawn", prefab);
		return;
	}

	NodeOwner::InstantiateContext ctx;
	ctx.resolver = [](UID id) { return assets::load<assets::Prefab>(id); };
	Box<Node> root = this->instantiate(file, ctx);
	if (not root.exists()) {
		TOAST_ERROR("World", "Failed to instantiate prefab {} to spawn", prefab);
		return;
	}

	root->m_uid.generate();
	root->m_parent = parent;
	parent.m_children.emplace_back(root);
	root->m_state = parent.m_state;
	root->m_type = NodeType::root;
	root->m_inherited_enabled = parent.enabled();

	root->propagateCallTick(root->info(), TickFunctionList::init);
	root->propagateCallTick(root->info(), TickFunctionList::begin);
	root->enabled(true);
	TOAST_TRACE("World", "Spawned node {} in {}", root->name(), parent.name());
}

void Workspace::eventSubscriptions() { }
}
