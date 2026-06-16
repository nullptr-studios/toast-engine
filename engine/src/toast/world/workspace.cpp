#include "workspace.hpp"

#include "node.hpp"
#include "toast/log.hpp"
#include "workspace_events.hpp"

#include <toast/assets/assets.hpp>
#include <toast/assets/prefab.hpp>

namespace toast {

Workspace::Workspace(std::string_view type, std::string_view name) {
	ZoneScoped;
	eventSubscriptions();

	// Allocation
	Box node = this->nodeAllocation(type);
	node->propagateCallTick(node->info(), TickFunctionList::load);
	node->propagateCallTick(node->info(), TickFunctionList::pre_init);

	// Data structure generation
	generateUid(node);
	node->m_parent = {};
	node->m_state = NodeState::root;
	node->m_type = NodeType::world_root;
	node->m_inherited_enabled = true;
	node->m_name = name;

	// Initialization
	node->callTick(node->info(), TickFunctionList::init);
	node->callTick(node->info(), TickFunctionList::begin);
	node->enabled(true);

	event::send<event::RequestHierarchyUpdate>();
	TOAST_INFO("World", "Created new workspace");
}

Workspace::Workspace(UID uid) {
	eventSubscriptions();

	// TODO:
	TOAST_NOT_IMPLEMENTED;
	TOAST_INFO("World", "Opened workspace from {}", uid);
}

void Workspace::registerDependency(Node& from, Node& to) {
	// TODO:
	TOAST_NOT_IMPLEMENTED;
}

auto Workspace::findFrom(const Node& origin, std::string_view query) -> Box<Node> {
	// TODO:
	TOAST_NOT_IMPLEMENTED;
	return {};
}

auto Workspace::searchFrom(const Node& origin, std::string_view query) -> std::vector<Box<Node>> {
	// TODO:
	TOAST_NOT_IMPLEMENTED;
	return {};
}

void Workspace::eventSubscriptions() {
	// Whenever we get notified that the hierarchy needs an update, send the update back to the editor
	m_listener.subscribe<event::RequestHierarchyUpdate>(
	    [this] {
		    event::send<event::UpdateHierarchyData>(m_root_node);
		    return true;
	    },
	    100
	);
}
}
