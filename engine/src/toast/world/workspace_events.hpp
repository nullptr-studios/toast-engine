/**
 * @file workspace_events.hpp
 * @author Xein
 * @date 16 Jun 2026
 */

#pragma once
#include <toast/events/event.hpp>
#include <toast/uid.hpp>
#include <toast/world/box.hpp>
#include <utility>

namespace event {

struct UpdateHierarchyData : Event<UpdateHierarchyData> {
	struct HierarchyElement {
		toast::UID uid;
		std::string name;
		std::string type;
		bool enabled;
		bool is_prefab;
		std::vector<HierarchyElement> children;

		HierarchyElement(const toast::Box<toast::Node>& node);

		HierarchyElement() = default;

		HierarchyElement(const HierarchyElement& other);
	};

	HierarchyElement root;
	bool is_empty = false;

	UpdateHierarchyData(const toast::Box<toast::Node>& node);

	UpdateHierarchyData(const HierarchyElement& h, bool is_empty) : root(h), is_empty(is_empty) { }
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

struct [[deprecated]] WorkspaceRemove : Event<WorkspaceRemove> {
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

struct WorkspaceAutosave : Event<WorkspaceAutosave> {
	uint64_t handle;
	std::string uri;

	WorkspaceAutosave(uint64_t handle, std::string uri) : handle(handle), uri(std::move(uri)) { }
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

struct WorkspaceDuplicateNode : Event<WorkspaceDuplicateNode> {
	toast::UID source;
	toast::UID parent;
};

struct WorkspaceCopyNode : Event<WorkspaceCopyNode> {
	toast::UID source;
};

struct WorkspacePasteNode : Event<WorkspacePasteNode> {
	toast::UID parent;
};

struct NodeChangeType : Event<NodeChangeType> {
	toast::UID node;
	std::string type;
};

struct WorkspacePromoteNode : Event<WorkspacePromoteNode> {
	toast::UID target;
	std::string path;
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

struct NodeChangeName : Event<NodeChangeName> {
	toast::UID node;
	std::string name;

	NodeChangeName(toast::UID n, std::string_view name) : node(n), name(name) { }
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

struct WorkspacePause : Event<WorkspacePause> {
	uint64_t handle;
	bool paused;

	WorkspacePause(uint64_t handle, bool paused) : handle(handle), paused(paused) { }
};

struct SetGizmoTool : Event<SetGizmoTool> {
	uint32_t tool;

	SetGizmoTool(uint32_t tool) : tool(tool) { }
};

struct SetCoordinateSpace : Event<SetCoordinateSpace> {
	bool world;

	SetCoordinateSpace(bool world) : world(world) { }
};

struct SetSnapping : Event<SetSnapping> {
	uint32_t kind;
	bool enabled;
	float value;

	SetSnapping(uint32_t kind, bool enabled, float value) : kind(kind), enabled(enabled), value(value) { }
};

struct SetCameraMode : Event<SetCameraMode> {
	bool game;

	SetCameraMode(bool game) : game(game) { }
};

struct InspectorContent : Event<InspectorContent> {
	struct InspectorField {
		std::string name;
		std::string value;

		InspectorField(std::string_view name, std::string_view value) : name(name), value(value) { }
	};

	std::string uid;
	std::string name;
	bool enabled;
	std::vector<InspectorField> parameters;

	InspectorContent(std::string_view uid, std::string_view name, bool enabled, std::vector<InspectorField> fields)
	    : uid(uid),
	      name(name),
	      enabled(enabled),
	      parameters(std::move(fields)) { }
};

}
