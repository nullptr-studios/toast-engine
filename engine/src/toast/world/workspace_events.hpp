/**
 * @file workspace_events.hpp
 * @author Xein
 * @date 16 Jun 2026
 */

#pragma once
#include <toast/events/event.hpp>
#include <toast/uid.hpp>
#include <toast/world/box.hpp>

namespace event {

struct UpdateHierarchyData : Event<UpdateHierarchyData> {
	struct HierarchyElement {
		toast::UID uid;
		std::string name;
		std::string type;
		bool enabled;
		std::vector<HierarchyElement> children;

		HierarchyElement(const toast::Box<toast::Node>& node);

		HierarchyElement() = default;

		HierarchyElement(const HierarchyElement& other);
	};

	HierarchyElement root;

	UpdateHierarchyData(const toast::Box<toast::Node>& node) : root(node) { }

	UpdateHierarchyData(const HierarchyElement& h) : root(h) { }
};

struct RequestHierarchyUpdate : Event<RequestHierarchyUpdate> { };

struct WorkspaceCreate : Event<WorkspaceCreate> {
	toast::UID parent;
	std::string type;

	WorkspaceCreate(toast::UID parent, std::string_view type) : parent(parent), type(type) { }
};

struct WorkspaceSpawn : Event<WorkspaceSpawn> {
	toast::UID parent;
	bool is_uri;
	toast::UID uid;
	std::string uri;

	WorkspaceSpawn() = default;

	WorkspaceSpawn(toast::UID parent, toast::UID uid) : parent(parent), is_uri(false), uid(uid) { }

	WorkspaceSpawn(toast::UID parent, std::string_view uri) : parent(parent), is_uri(true), uri(uri) { }
};

struct WorkspaceRemove : Event<WorkspaceRemove> {
	toast::UID target;

	WorkspaceRemove(toast::UID target) : target(target) { }
};

struct WorkspaceDestroy : Event<WorkspaceDestroy> {
	uint64_t handle;

	WorkspaceDestroy(uint64_t handle) : handle(handle) { }
};

struct SetActiveWorkspace : Event<SetActiveWorkspace> {
	uint64_t handle;

	SetActiveWorkspace(uint64_t handle) : handle(handle) { }
};

struct WorkspaceSave : Event<WorkspaceSave> {
	toast::UID target;
	std::string uri;

	WorkspaceSave(toast::UID target, std::string uri) : target(target), uri(std::move(uri)) { }
};

struct WorkspaceCreateNode : Event<WorkspaceCreateNode> {
	toast::UID parent;
	std::string type;

	WorkspaceCreateNode(toast::UID parent, std::string_view type) : parent(parent), type(type) { }
};

struct WorkspaceRemoveNode : Event<WorkspaceRemoveNode> {
	toast::UID target;

	WorkspaceRemoveNode(toast::UID target) : target(target) { }
};

struct WorkspaceMoveNodeTo : Event<WorkspaceMoveNodeTo> {
	toast::UID target;
	toast::UID new_parent;
	toast::UID predecessor;

	WorkspaceMoveNodeTo(toast::UID target, toast::UID new_parent, toast::UID predecessor)
	    : target(target),
	      new_parent(new_parent),
	      predecessor(predecessor) { }
};

struct SetFocusedNode : Event<SetFocusedNode> {
	toast::UID node;

	SetFocusedNode(toast::UID n) : node(n) { }
};

struct NodeChangeParam : Event<NodeChangeParam> {
	std::string parameter;
	std::string value;

	NodeChangeParam(std::string_view parameter, std::string_view value) : parameter(parameter), value(value) { }
};

struct NodeCallFunction : Event<NodeCallFunction> {
	std::string function;

	NodeCallFunction(std::string_view f) : function(f) { }
};

struct NodeEnabled : Event<NodeEnabled> {
	toast::UID node;
	bool enabled;

	NodeEnabled(toast::UID n, bool enabled) : node(n), enabled(enabled) { }
};

}
