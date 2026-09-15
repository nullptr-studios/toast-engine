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
	uint64_t workspace_handle = 0;
	uint64_t request = 0;

	WorkspaceSave(toast::UID target, std::string uri, uint64_t workspace_handle = 0, uint64_t request = 0)
	    : target(target),
	      uri(std::move(uri)),
	      workspace_handle(workspace_handle),
	      request(request) { }
};

struct WorkspaceSaveCompleted : Event<WorkspaceSaveCompleted> {
	uint64_t workspace_handle = 0;
	uint64_t request = 0;
	bool success = false;
	std::vector<uint8_t> snapshot;
	std::string error;
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
	toast::UID node;
	std::string parameter;
	std::string value;

	NodeChangeParam(toast::UID node, std::string_view parameter, std::string_view value)
	    : node(node),
	      parameter(parameter),
	      value(value) { }
};

struct NodeChangeName : Event<NodeChangeName> {
	toast::UID node;
	std::string name;

	NodeChangeName(toast::UID n, std::string_view name) : node(n), name(name) { }
};

struct NodeCallFunction : Event<NodeCallFunction> {
	toast::UID node;
	std::string function;

	NodeCallFunction(toast::UID node, std::string_view f) : node(node), function(f) { }
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

struct InspectorLuaContent : Event<InspectorLuaContent> {
	struct LuaField {
		std::string path;
		std::string name;
		uint32_t kind = 0;
		bool is_array = false;
		std::string ref_type;
		std::string value;
		std::string default_value;
	};

	struct LuaSubgroup {
		std::string name;
		std::vector<LuaField> fields;
	};

	struct LuaGroup {
		std::string name;
		std::vector<LuaField> fields;
		std::vector<LuaSubgroup> subgroups;
	};

	struct LuaScriptCard {
		std::string script;
		std::vector<LuaField> fields;
		std::vector<LuaGroup> groups;
	};

	std::string uid;
	uint32_t schema_version = 0;
	std::vector<LuaScriptCard> scripts;

	InspectorLuaContent() = default;

	InspectorLuaContent(std::string_view uid, uint32_t schema_version, std::vector<LuaScriptCard> scripts)
	    : uid(uid),
	      schema_version(schema_version),
	      scripts(std::move(scripts)) { }
};

struct NodeChangeLuaParam : Event<NodeChangeLuaParam> {
	toast::UID node;
	std::string path;
	std::string value;

	NodeChangeLuaParam(toast::UID node, std::string_view path, std::string_view value) : node(node), path(path), value(value) { }
};

enum class HistoryOperation : uint8_t {
	initial,
	create,
	remove,
	rename,
	enable,
	change_value,
	move,
	reparent,
	retype,
	duplicate,
	spawn_prefab,
	paste,
	promote,
	call_function,
	cherry_pick,
	merge
};

struct HistoryRevision {
	uint64_t id = 0;
	uint64_t sequence = 0;
	std::vector<uint64_t> parents;
	toast::UID node_uid;
	std::string node_name;
	HistoryOperation operation = HistoryOperation::initial;
	std::string subject;
	std::string previous_value;
	std::string current_value;
};

struct WorkspaceHistoryInitialSnapshot : Event<WorkspaceHistoryInitialSnapshot> {
	uint64_t workspace_handle = 0;
	std::vector<uint8_t> snapshot;
	bool available = true;
	bool initially_saved = false;
	std::string unavailable_reason;
};

struct WorkspaceHistoryCommitted : Event<WorkspaceHistoryCommitted> {
	uint64_t workspace_handle = 0;
	std::vector<uint8_t> before_snapshot;
	std::vector<uint8_t> after_snapshot;
	toast::UID node_uid;
	std::string node_name;
	HistoryOperation operation = HistoryOperation::change_value;
	std::string subject;
	std::string previous_value;
	std::string current_value;
};

struct WorkspaceApplyHistorySnapshot : Event<WorkspaceApplyHistorySnapshot> {
	uint64_t workspace_handle = 0;
	uint64_t request = 0;
	std::vector<uint8_t> snapshot;
};

struct WorkspaceHistorySnapshotApplied : Event<WorkspaceHistorySnapshotApplied> {
	uint64_t workspace_handle = 0;
	uint64_t request = 0;
	bool success = false;
	std::string error;
};

struct WorkspacePrepareHistoryMerge : Event<WorkspacePrepareHistoryMerge> {
	uint64_t workspace_handle = 0;
	uint64_t request = 0;
	bool is_merge = false;
	std::vector<uint8_t> base_snapshot;
	std::vector<uint8_t> current_snapshot;
	std::vector<uint8_t> incoming_snapshot;
};

struct WorkspaceHistoryMergePrepared : Event<WorkspaceHistoryMergePrepared> {
	uint64_t workspace_handle = 0;
	uint64_t request = 0;
	bool success = false;
	std::vector<uint8_t> snapshot;
	std::string error;
};

struct UpdateWorkspaceHistory : Event<UpdateWorkspaceHistory> {
	uint64_t workspace_handle = 0;
	bool available = true;
	std::string unavailable_reason;
	uint64_t current_revision = 0;
	bool is_dirty = false;
	bool transaction_open = false;
	std::vector<HistoryRevision> revisions;
};

struct RequestWorkspaceHistory : Event<RequestWorkspaceHistory> {
	uint64_t workspace_handle = 0;
};

struct WorkspaceUndo : Event<WorkspaceUndo> {
	uint64_t workspace_handle = 0;
};

struct WorkspaceRedo : Event<WorkspaceRedo> {
	uint64_t workspace_handle = 0;
};

struct WorkspaceCheckoutHistory : Event<WorkspaceCheckoutHistory> {
	uint64_t workspace_handle = 0;
	uint64_t revision = 0;
};

struct WorkspaceCherryPickHistory : Event<WorkspaceCherryPickHistory> {
	uint64_t workspace_handle = 0;
	uint64_t revision = 0;
};

struct WorkspaceMergeHistory : Event<WorkspaceMergeHistory> {
	uint64_t workspace_handle = 0;
	uint64_t revision = 0;
};

struct WorkspaceHistoryTransaction : Event<WorkspaceHistoryTransaction> {
	enum class Phase : uint8_t {
		begin,
		commit,
		cancel
	};
	uint64_t workspace_handle = 0;
	uint64_t transaction = 0;
	Phase phase = Phase::begin;
	HistoryOperation operation = HistoryOperation::change_value;
	toast::UID node;
	std::string subject;
};

enum class HistoryConflictKind : uint8_t {
	value,
	name,
	type,
	enabled,
	parent,
	order,
	existence,
	lua_value
};

struct HistoryConflict {
	uint64_t id = 0;
	toast::UID node_uid;
	std::string node_name;
	HistoryConflictKind kind = HistoryConflictKind::value;
	std::string field;
	uint32_t value_type = 0;
	bool is_array = false;
	std::string ref_type;
	std::string base_value;
	std::string current_value;
	std::string incoming_value;
};

struct WorkspaceHistoryConflicts : Event<WorkspaceHistoryConflicts> {
	uint64_t workspace_handle = 0;
	uint64_t request = 0;
	bool is_merge = false;
	uint64_t source_revision = 0;
	std::vector<HistoryConflict> conflicts;
};

struct HistoryConflictResolution {
	enum class Choice : uint8_t {
		base,
		current,
		incoming,
		custom_value
	};
	uint64_t conflict = 0;
	Choice choice = Choice::current;
	std::string custom_value;
};

struct WorkspaceResolveHistoryConflicts : Event<WorkspaceResolveHistoryConflicts> {
	uint64_t workspace_handle = 0;
	uint64_t request = 0;
	bool cancel = false;
	std::vector<HistoryConflictResolution> resolutions;
};

}
