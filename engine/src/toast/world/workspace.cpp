#include "workspace.hpp"

#include "node.hpp"
#include "toast/log.hpp"
#include "workspace_events.hpp"

#include <toast/assets/assets.hpp>
#include <toast/assets/prefab.hpp>

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
