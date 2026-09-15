#include "workspace_events.hpp"

#include <generated/workspace_events.pb.h>
#include <limits>
#include <toast/assets/assets.hpp>
#include <toast/events/proto_event.hpp>
#include <toast/world/node.hpp>

namespace event {

UpdateHierarchyData::HierarchyElement::HierarchyElement(const toast::Box<toast::Node>& node) {
	uid = node->uid();
	name = node->name();
	type = node->info()->type;
	enabled = node->enabled();
	is_prefab = node->type() == toast::NodeType::root;
	children.reserve(node->children().size());
	for (const auto& c : node->children()) {
		// TODO: not go down if its a prefab
		children.emplace_back(c);
	}
}

UpdateHierarchyData::HierarchyElement::HierarchyElement(const HierarchyElement& other) {
	uid = other.uid;
	name = other.name;
	type = other.type;
	enabled = other.enabled;
	children = other.children;
	is_prefab = other.is_prefab;
}

UpdateHierarchyData::UpdateHierarchyData(const toast::Box<toast::Node>& node) {
	if (node.exists()) {
		root = HierarchyElement(node);
	} else {
		is_empty = true;
	}
}

template<>
struct ProtoTraits<UpdateHierarchyData::HierarchyElement> {
	using Proto = proto::events::HierarchyElement;
	using Event = UpdateHierarchyData::HierarchyElement;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_uid(e.uid);
		p.set_name(e.name);
		p.set_type(e.type);
		p.set_enabled(e.enabled);
		p.set_is_prefab(e.is_prefab);
		for (const auto& c : e.children) {
			auto* element = p.add_children();
			*element = toProto(c);
		}
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.uid = toast::UID::fromString(p.uid());
		e.name = p.name();
		e.type = p.type();
		e.enabled = p.enabled();
		e.is_prefab = p.is_prefab();
		e.children.reserve(p.children_size());
		for (const auto& c : p.children()) {
			e.children.emplace_back(fromProto(c));
		}
		return e;
	}
};

template<>
struct ProtoTraits<UpdateHierarchyData> {
	using Proto = proto::events::UpdateHierarchyData;
	using Event = UpdateHierarchyData;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_is_empty(e.is_empty);
		*p.mutable_root() = ProtoTraits<UpdateHierarchyData::HierarchyElement>::toProto(e.root);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		return {ProtoTraits<UpdateHierarchyData::HierarchyElement>::fromProto(p.root()), p.is_empty()};
	}
};

TOAST_PROTO_EVENT(UpdateHierarchyData);

template<>
struct ProtoTraits<RequestHierarchyUpdate> {
	using Proto = proto::events::RequestHierarchyUpdate;
	using Event = RequestHierarchyUpdate;

	static auto toProto(const Event& e) -> Proto { return {}; }

	static auto fromProto(const Proto& p) -> Event { return {}; }
};

TOAST_PROTO_EVENT(RequestHierarchyUpdate);

template<>
struct ProtoTraits<ReloadAssetsManifest> {
	using Proto = proto::events::ReloadAssetsManifest;
	using Event = ReloadAssetsManifest;

	static auto toProto(const Event& e) -> Proto { return {}; }

	static auto fromProto(const Proto& p) -> Event { return {}; }
};

TOAST_PROTO_EVENT(ReloadAssetsManifest);

template<>
struct ProtoTraits<WorkspaceCreate> {
	using Proto = proto::events::WorkspaceCreate;
	using Event = WorkspaceCreate;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_parent(e.parent);
		p.set_type(e.type);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return {toast::UID::fromString(p.parent()), p.type()}; }
};

TOAST_PROTO_EVENT(WorkspaceCreate);

template<>
struct ProtoTraits<WorkspaceSpawn> {
	using Proto = proto::events::WorkspaceSpawn;
	using Event = WorkspaceSpawn;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_parent(e.parent);
		p.set_is_uri(e.is_uri);
		p.set_uid(!e.is_uri ? e.uid : toast::UID {0});
		p.set_uri(e.uri);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.parent = toast::UID::fromString(p.parent());
		e.is_uri = p.is_uri();
		e.uid = !e.is_uri ? toast::UID::fromString(p.uid()) : toast::UID {0};
		e.uri = p.uri();
		return e;
	}
};

TOAST_PROTO_EVENT(WorkspaceSpawn);

// template<>
// struct ProtoTraits<WorkspaceRemove> {
// 	using Proto = proto::events::WorkspaceRemove;
// 	using Event = WorkspaceRemove;
//
// 	static auto toProto(const Event& e) -> Proto {
// 		Proto p;
// 		p.set_target(e.target);
// 		return p;
// 	}
//
// 	static auto fromProto(const Proto& p) -> Event { return {toast::UID::fromString(p.target())}; }
// };
//
// TOAST_PROTO_EVENT(WorkspaceRemove);

template<>
struct ProtoTraits<WorkspaceDestroy> {
	using Proto = proto::events::WorkspaceDestroy;
	using Event = WorkspaceDestroy;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_handle(e.handle);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return {p.handle()}; }
};

TOAST_PROTO_EVENT(WorkspaceDestroy);

template<>
struct ProtoTraits<SetActiveWorkspace> {
	using Proto = proto::events::SetActiveWorkspace;
	using Event = SetActiveWorkspace;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_handle(e.handle);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return {p.handle()}; }
};

TOAST_PROTO_EVENT(SetActiveWorkspace);

template<>
struct ProtoTraits<WorkspaceSave> {
	using Proto = proto::events::WorkspaceSave;
	using Event = WorkspaceSave;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_target(e.target);
		p.set_path(e.uri);
		p.set_workspace_handle(e.workspace_handle);
		p.set_request(e.request);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		return {toast::UID::fromString(p.target()), p.path(), p.workspace_handle(), p.request()};
	}
};

TOAST_PROTO_EVENT(WorkspaceSave);

template<>
struct ProtoTraits<WorkspaceSaveCompleted> {
	using Proto = proto::events::WorkspaceSaveCompleted;
	using Event = WorkspaceSaveCompleted;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_workspace_handle(e.workspace_handle);
		p.set_request(e.request);
		p.set_success(e.success);
		p.set_snapshot(e.snapshot.data(), e.snapshot.size());
		p.set_error(e.error);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.workspace_handle = p.workspace_handle();
		e.request = p.request();
		e.success = p.success();
		e.snapshot.assign(p.snapshot().begin(), p.snapshot().end());
		e.error = p.error();
		return e;
	}
};

TOAST_PROTO_EVENT(WorkspaceSaveCompleted);

template<>
struct ProtoTraits<WorkspaceAutosave> {
	using Proto = proto::events::WorkspaceAutosave;
	using Event = WorkspaceAutosave;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_handle(e.handle);
		p.set_path(e.uri);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return {p.handle(), p.path()}; }
};

TOAST_PROTO_EVENT(WorkspaceAutosave);

template<>
struct ProtoTraits<WorkspaceCreateNode> {
	using Proto = proto::events::WorkspaceCreateNode;
	using Event = WorkspaceCreateNode;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_parent(e.parent);
		p.set_type(e.type);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return {toast::UID::fromString(p.parent()), p.type()}; }
};

TOAST_PROTO_EVENT(WorkspaceCreateNode);

template<>
struct ProtoTraits<WorkspaceRemoveNode> {
	using Proto = proto::events::WorkspaceRemoveNode;
	using Event = WorkspaceRemoveNode;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_target(e.target);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return {toast::UID::fromString(p.target())}; }
};

TOAST_PROTO_EVENT(WorkspaceRemoveNode);

template<>
struct ProtoTraits<WorkspaceMoveNodeTo> {
	using Proto = proto::events::WorkspaceMoveNodeTo;
	using Event = WorkspaceMoveNodeTo;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_target(e.target);
		p.set_new_parent(e.new_parent);
		p.set_predecessor(e.predecessor);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		toast::UID predecessor_uid = 0;
		if (!p.predecessor().empty()) {
			predecessor_uid =
			    (p.predecessor() == "end") ? std::numeric_limits<uint64_t>::max() : toast::UID::fromString(p.predecessor());
		}
		return {
		  toast::UID::fromString(p.target()),
		  p.new_parent().empty() ? toast::UID(0) : toast::UID::fromString(p.new_parent()),
		  predecessor_uid
		};
	}
};

TOAST_PROTO_EVENT(WorkspaceMoveNodeTo);

template<>
struct ProtoTraits<SetFocusedNode> {
	using Proto = proto::events::SetFocusedNode;
	using Event = SetFocusedNode;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_node(e.node);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return {toast::UID::fromString(p.node())}; }
};

TOAST_PROTO_EVENT(SetFocusedNode);

template<>
struct ProtoTraits<NodeChangeParam> {
	using Proto = proto::events::NodeChangeParam;
	using Event = NodeChangeParam;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_node(e.node);
		p.set_parameter(e.parameter);
		p.set_value(e.value);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return {toast::UID::fromString(p.node()), p.parameter(), p.value()}; }
};

TOAST_PROTO_EVENT(NodeChangeParam);

template<>
struct ProtoTraits<NodeChangeName> {
	using Proto = proto::events::NodeChangeName;
	using Event = NodeChangeName;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_node(e.node);
		p.set_name(e.name);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return {toast::UID::fromString(p.node()), p.name()}; }
};

TOAST_PROTO_EVENT(NodeChangeName);

template<>
struct ProtoTraits<NodeCallFunction> {
	using Proto = proto::events::NodeCallFunction;
	using Event = NodeCallFunction;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_node(e.node);
		p.set_function(e.function);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		return Event {toast::UID::fromString(p.node()), std::string_view {p.function()}};
	}
};

TOAST_PROTO_EVENT(NodeCallFunction);

template<>
struct ProtoTraits<NodeEnabled> {
	using Proto = proto::events::NodeEnabled;
	using Event = NodeEnabled;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_node(e.node);
		p.set_enabled(e.enabled);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return {toast::UID::fromString(p.node()), p.enabled()}; }
};

TOAST_PROTO_EVENT(NodeEnabled);

template<>
struct ProtoTraits<WorkspacePause> {
	using Proto = proto::events::WorkspacePause;
	using Event = WorkspacePause;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_handle(e.handle);
		p.set_paused(e.paused);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return {p.handle(), p.paused()}; }
};

TOAST_PROTO_EVENT(WorkspacePause);

template<>
struct ProtoTraits<SetGizmoTool> {
	using Proto = proto::events::SetGizmoTool;
	using Event = SetGizmoTool;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_tool(e.tool);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return {p.tool()}; }
};

TOAST_PROTO_EVENT(SetGizmoTool);

template<>
struct ProtoTraits<SetCoordinateSpace> {
	using Proto = proto::events::SetCoordinateSpace;
	using Event = SetCoordinateSpace;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_world(e.world);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return {p.world()}; }
};

TOAST_PROTO_EVENT(SetCoordinateSpace);

template<>
struct ProtoTraits<SetSnapping> {
	using Proto = proto::events::SetSnapping;
	using Event = SetSnapping;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_kind(e.kind);
		p.set_enabled(e.enabled);
		p.set_value(e.value);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return {p.kind(), p.enabled(), p.value()}; }
};

TOAST_PROTO_EVENT(SetSnapping);

template<>
struct ProtoTraits<SetCameraMode> {
	using Proto = proto::events::SetCameraMode;
	using Event = SetCameraMode;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_game(e.game);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return {p.game()}; }
};

TOAST_PROTO_EVENT(SetCameraMode);

template<>
struct ProtoTraits<InspectorContent::InspectorField> {
	using Proto = proto::events::InspectorField;
	using Event = InspectorContent::InspectorField;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_name(e.name);
		p.set_value(e.value);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return {p.name(), p.value()}; }
};

template<>
struct ProtoTraits<InspectorContent> {
	using Proto = proto::events::InspectorContent;
	using Event = InspectorContent;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_uid(e.uid);
		p.set_name(e.name);
		p.set_enabled(e.enabled);
		for (const auto& f : e.parameters) {
			auto* element = p.add_parameters();
			*element = ProtoTraits<InspectorContent::InspectorField>::toProto(f);
		}
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		std::vector<InspectorContent::InspectorField> parameters;
		parameters.reserve(p.parameters().size());
		for (const auto& f : p.parameters()) {
			parameters.emplace_back(ProtoTraits<InspectorContent::InspectorField>::fromProto(f));
		}
		return {p.uid(), p.name(), p.enabled(), parameters};
	}
};

TOAST_PROTO_EVENT(InspectorContent);

template<>
struct ProtoTraits<InspectorLuaContent::LuaField> {
	using Proto = proto::events::LuaField;
	using Event = InspectorLuaContent::LuaField;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_path(e.path);
		p.set_name(e.name);
		p.set_kind(e.kind);
		p.set_is_array(e.is_array);
		p.set_ref_type(e.ref_type);
		p.set_value(e.value);
		p.set_default_value(e.default_value);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		return {p.path(), p.name(), p.kind(), p.is_array(), p.ref_type(), p.value(), p.default_value()};
	}
};

template<>
struct ProtoTraits<InspectorLuaContent::LuaSubgroup> {
	using Proto = proto::events::LuaSubgroup;
	using Event = InspectorLuaContent::LuaSubgroup;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_name(e.name);
		for (const auto& f : e.fields) {
			*p.add_fields() = ProtoTraits<InspectorLuaContent::LuaField>::toProto(f);
		}
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.name = p.name();
		e.fields.reserve(p.fields_size());
		for (const auto& f : p.fields()) {
			e.fields.emplace_back(ProtoTraits<InspectorLuaContent::LuaField>::fromProto(f));
		}
		return e;
	}
};

template<>
struct ProtoTraits<InspectorLuaContent::LuaGroup> {
	using Proto = proto::events::LuaGroup;
	using Event = InspectorLuaContent::LuaGroup;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_name(e.name);
		for (const auto& f : e.fields) {
			*p.add_fields() = ProtoTraits<InspectorLuaContent::LuaField>::toProto(f);
		}
		for (const auto& s : e.subgroups) {
			*p.add_subgroups() = ProtoTraits<InspectorLuaContent::LuaSubgroup>::toProto(s);
		}
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.name = p.name();
		e.fields.reserve(p.fields_size());
		for (const auto& f : p.fields()) {
			e.fields.emplace_back(ProtoTraits<InspectorLuaContent::LuaField>::fromProto(f));
		}
		e.subgroups.reserve(p.subgroups_size());
		for (const auto& s : p.subgroups()) {
			e.subgroups.emplace_back(ProtoTraits<InspectorLuaContent::LuaSubgroup>::fromProto(s));
		}
		return e;
	}
};

template<>
struct ProtoTraits<InspectorLuaContent::LuaScriptCard> {
	using Proto = proto::events::LuaScriptCard;
	using Event = InspectorLuaContent::LuaScriptCard;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_script(e.script);
		for (const auto& f : e.fields) {
			*p.add_fields() = ProtoTraits<InspectorLuaContent::LuaField>::toProto(f);
		}
		for (const auto& g : e.groups) {
			*p.add_groups() = ProtoTraits<InspectorLuaContent::LuaGroup>::toProto(g);
		}
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.script = p.script();
		e.fields.reserve(p.fields_size());
		for (const auto& f : p.fields()) {
			e.fields.emplace_back(ProtoTraits<InspectorLuaContent::LuaField>::fromProto(f));
		}
		e.groups.reserve(p.groups_size());
		for (const auto& g : p.groups()) {
			e.groups.emplace_back(ProtoTraits<InspectorLuaContent::LuaGroup>::fromProto(g));
		}
		return e;
	}
};

template<>
struct ProtoTraits<InspectorLuaContent> {
	using Proto = proto::events::InspectorLuaContent;
	using Event = InspectorLuaContent;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_uid(e.uid);
		p.set_schema_version(e.schema_version);
		for (const auto& s : e.scripts) {
			*p.add_scripts() = ProtoTraits<InspectorLuaContent::LuaScriptCard>::toProto(s);
		}
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		std::vector<InspectorLuaContent::LuaScriptCard> scripts;
		scripts.reserve(p.scripts_size());
		for (const auto& s : p.scripts()) {
			scripts.emplace_back(ProtoTraits<InspectorLuaContent::LuaScriptCard>::fromProto(s));
		}
		return {p.uid(), p.schema_version(), std::move(scripts)};
	}
};

TOAST_PROTO_EVENT(InspectorLuaContent);

template<>
struct ProtoTraits<NodeChangeLuaParam> {
	using Proto = proto::events::NodeChangeLuaParam;
	using Event = NodeChangeLuaParam;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_node(e.node);
		p.set_path(e.path);
		p.set_value(e.value);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return {toast::UID::fromString(p.node()), p.path(), p.value()}; }
};

TOAST_PROTO_EVENT(NodeChangeLuaParam);

template<>
struct ProtoTraits<WorkspaceDuplicateNode> {
	using Proto = proto::events::WorkspaceDuplicateNode;
	using Event = WorkspaceDuplicateNode;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_source(e.source);
		p.set_parent(e.parent);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.source = toast::UID::fromString(p.source());
		e.parent = toast::UID::fromString(p.parent());
		return e;
	}
};

TOAST_PROTO_EVENT(WorkspaceDuplicateNode);

template<>
struct ProtoTraits<WorkspaceCopyNode> {
	using Proto = proto::events::WorkspaceCopyNode;
	using Event = WorkspaceCopyNode;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_source(e.source);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.source = toast::UID::fromString(p.source());
		return e;
	}
};

TOAST_PROTO_EVENT(WorkspaceCopyNode);

template<>
struct ProtoTraits<WorkspacePasteNode> {
	using Proto = proto::events::WorkspacePasteNode;
	using Event = WorkspacePasteNode;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_parent(e.parent);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.parent = toast::UID::fromString(p.parent());
		return e;
	}
};

TOAST_PROTO_EVENT(WorkspacePasteNode);

template<>
struct ProtoTraits<NodeChangeType> {
	using Proto = proto::events::NodeChangeType;
	using Event = NodeChangeType;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_node(e.node);
		p.set_type(e.type);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.node = toast::UID::fromString(p.node());
		e.type = p.type();
		return e;
	}
};

TOAST_PROTO_EVENT(NodeChangeType);

template<>
struct ProtoTraits<WorkspacePromoteNode> {
	using Proto = proto::events::WorkspacePromoteNode;
	using Event = WorkspacePromoteNode;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_target(e.target);
		p.set_path(e.path);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.target = toast::UID::fromString(p.target());
		e.path = p.path();
		return e;
	}
};

TOAST_PROTO_EVENT(WorkspacePromoteNode);

template<>
struct ProtoTraits<HistoryRevision> {
	using Proto = proto::events::HistoryRevision;
	using Event = HistoryRevision;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_id(e.id);
		p.set_sequence(e.sequence);
		for (auto parent : e.parents) {
			p.add_parents(parent);
		}
		p.set_node_uid(e.node_uid);
		p.set_node_name(e.node_name);
		p.set_operation(static_cast<proto::events::HistoryOperation>(e.operation));
		p.set_subject(e.subject);
		p.set_previous_value(e.previous_value);
		p.set_current_value(e.current_value);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.id = p.id();
		e.sequence = p.sequence();
		e.parents.assign(p.parents().begin(), p.parents().end());
		e.node_uid = toast::UID::fromString(p.node_uid());
		e.node_name = p.node_name();
		e.operation = static_cast<HistoryOperation>(p.operation());
		e.subject = p.subject();
		e.previous_value = p.previous_value();
		e.current_value = p.current_value();
		return e;
	}
};

template<>
struct ProtoTraits<WorkspaceHistoryInitialSnapshot> {
	using Proto = proto::events::WorkspaceHistoryInitialSnapshot;
	using Event = WorkspaceHistoryInitialSnapshot;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_workspace_handle(e.workspace_handle);
		p.set_snapshot(e.snapshot.data(), e.snapshot.size());
		p.set_available(e.available);
		p.set_initially_saved(e.initially_saved);
		p.set_unavailable_reason(e.unavailable_reason);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.workspace_handle = p.workspace_handle();
		e.snapshot.assign(p.snapshot().begin(), p.snapshot().end());
		e.available = p.available();
		e.initially_saved = p.initially_saved();
		e.unavailable_reason = p.unavailable_reason();
		return e;
	}
};

TOAST_PROTO_EVENT(WorkspaceHistoryInitialSnapshot);

template<>
struct ProtoTraits<WorkspaceHistoryCommitted> {
	using Proto = proto::events::WorkspaceHistoryCommitted;
	using Event = WorkspaceHistoryCommitted;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_workspace_handle(e.workspace_handle);
		p.set_before_snapshot(e.before_snapshot.data(), e.before_snapshot.size());
		p.set_after_snapshot(e.after_snapshot.data(), e.after_snapshot.size());
		p.set_node_uid(e.node_uid);
		p.set_node_name(e.node_name);
		p.set_operation(static_cast<proto::events::HistoryOperation>(e.operation));
		p.set_subject(e.subject);
		p.set_previous_value(e.previous_value);
		p.set_current_value(e.current_value);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.workspace_handle = p.workspace_handle();
		e.before_snapshot.assign(p.before_snapshot().begin(), p.before_snapshot().end());
		e.after_snapshot.assign(p.after_snapshot().begin(), p.after_snapshot().end());
		e.node_uid = toast::UID::fromString(p.node_uid());
		e.node_name = p.node_name();
		e.operation = static_cast<HistoryOperation>(p.operation());
		e.subject = p.subject();
		e.previous_value = p.previous_value();
		e.current_value = p.current_value();
		return e;
	}
};

TOAST_PROTO_EVENT(WorkspaceHistoryCommitted);

template<>
struct ProtoTraits<WorkspaceApplyHistorySnapshot> {
	using Proto = proto::events::WorkspaceApplyHistorySnapshot;
	using Event = WorkspaceApplyHistorySnapshot;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_workspace_handle(e.workspace_handle);
		p.set_request(e.request);
		p.set_snapshot(e.snapshot.data(), e.snapshot.size());
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.workspace_handle = p.workspace_handle();
		e.request = p.request();
		e.snapshot.assign(p.snapshot().begin(), p.snapshot().end());
		return e;
	}
};

TOAST_PROTO_EVENT(WorkspaceApplyHistorySnapshot);

template<>
struct ProtoTraits<WorkspaceHistorySnapshotApplied> {
	using Proto = proto::events::WorkspaceHistorySnapshotApplied;
	using Event = WorkspaceHistorySnapshotApplied;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_workspace_handle(e.workspace_handle);
		p.set_request(e.request);
		p.set_success(e.success);
		p.set_error(e.error);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.workspace_handle = p.workspace_handle();
		e.request = p.request();
		e.success = p.success();
		e.error = p.error();
		return e;
	}
};

TOAST_PROTO_EVENT(WorkspaceHistorySnapshotApplied);

template<>
struct ProtoTraits<WorkspacePrepareHistoryMerge> {
	using Proto = proto::events::WorkspacePrepareHistoryMerge;
	using Event = WorkspacePrepareHistoryMerge;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_workspace_handle(e.workspace_handle);
		p.set_request(e.request);
		p.set_is_merge(e.is_merge);
		p.set_base_snapshot(e.base_snapshot.data(), e.base_snapshot.size());
		p.set_current_snapshot(e.current_snapshot.data(), e.current_snapshot.size());
		p.set_incoming_snapshot(e.incoming_snapshot.data(), e.incoming_snapshot.size());
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.workspace_handle = p.workspace_handle();
		e.request = p.request();
		e.is_merge = p.is_merge();
		e.base_snapshot.assign(p.base_snapshot().begin(), p.base_snapshot().end());
		e.current_snapshot.assign(p.current_snapshot().begin(), p.current_snapshot().end());
		e.incoming_snapshot.assign(p.incoming_snapshot().begin(), p.incoming_snapshot().end());
		return e;
	}
};

TOAST_PROTO_EVENT(WorkspacePrepareHistoryMerge);

template<>
struct ProtoTraits<WorkspaceHistoryMergePrepared> {
	using Proto = proto::events::WorkspaceHistoryMergePrepared;
	using Event = WorkspaceHistoryMergePrepared;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_workspace_handle(e.workspace_handle);
		p.set_request(e.request);
		p.set_success(e.success);
		p.set_snapshot(e.snapshot.data(), e.snapshot.size());
		p.set_error(e.error);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.workspace_handle = p.workspace_handle();
		e.request = p.request();
		e.success = p.success();
		e.snapshot.assign(p.snapshot().begin(), p.snapshot().end());
		e.error = p.error();
		return e;
	}
};

TOAST_PROTO_EVENT(WorkspaceHistoryMergePrepared);

template<>
struct ProtoTraits<UpdateWorkspaceHistory> {
	using Proto = proto::events::UpdateWorkspaceHistory;
	using Event = UpdateWorkspaceHistory;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_workspace_handle(e.workspace_handle);
		p.set_available(e.available);
		p.set_unavailable_reason(e.unavailable_reason);
		p.set_current_revision(e.current_revision);
		p.set_is_dirty(e.is_dirty);
		p.set_transaction_open(e.transaction_open);
		for (const auto& revision : e.revisions) {
			*p.add_revisions() = ProtoTraits<HistoryRevision>::toProto(revision);
		}
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.workspace_handle = p.workspace_handle();
		e.available = p.available();
		e.unavailable_reason = p.unavailable_reason();
		e.current_revision = p.current_revision();
		e.is_dirty = p.is_dirty();
		e.transaction_open = p.transaction_open();
		for (const auto& revision : p.revisions()) {
			e.revisions.push_back(ProtoTraits<HistoryRevision>::fromProto(revision));
		}
		return e;
	}
};

TOAST_PROTO_EVENT(UpdateWorkspaceHistory);

#define TOAST_HISTORY_HANDLE_EVENT(Type)             \
	template<>                                         \
	struct ProtoTraits<Type> {                         \
		using Proto = proto::events::Type;               \
		using Event = Type;                              \
		static auto toProto(const Event& e) -> Proto {   \
			Proto p;                                       \
			p.set_workspace_handle(e.workspace_handle);    \
			return p;                                      \
		}                                                \
		static auto fromProto(const Proto& p) -> Event { \
			Event e;                                       \
			e.workspace_handle = p.workspace_handle();     \
			return e;                                      \
		}                                                \
	};                                                 \
	TOAST_PROTO_EVENT(Type)

TOAST_HISTORY_HANDLE_EVENT(RequestWorkspaceHistory);
TOAST_HISTORY_HANDLE_EVENT(WorkspaceUndo);
TOAST_HISTORY_HANDLE_EVENT(WorkspaceRedo);

#define TOAST_HISTORY_REVISION_EVENT(Type)           \
	template<>                                         \
	struct ProtoTraits<Type> {                         \
		using Proto = proto::events::Type;               \
		using Event = Type;                              \
		static auto toProto(const Event& e) -> Proto {   \
			Proto p;                                       \
			p.set_workspace_handle(e.workspace_handle);    \
			p.set_revision(e.revision);                    \
			return p;                                      \
		}                                                \
		static auto fromProto(const Proto& p) -> Event { \
			Event e;                                       \
			e.workspace_handle = p.workspace_handle();     \
			e.revision = p.revision();                     \
			return e;                                      \
		}                                                \
	};                                                 \
	TOAST_PROTO_EVENT(Type)

TOAST_HISTORY_REVISION_EVENT(WorkspaceCheckoutHistory);
TOAST_HISTORY_REVISION_EVENT(WorkspaceCherryPickHistory);
TOAST_HISTORY_REVISION_EVENT(WorkspaceMergeHistory);

template<>
struct ProtoTraits<WorkspaceHistoryTransaction> {
	using Proto = proto::events::WorkspaceHistoryTransaction;
	using Event = WorkspaceHistoryTransaction;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_workspace_handle(e.workspace_handle);
		p.set_transaction(e.transaction);
		p.set_phase(static_cast<Proto::Phase>(e.phase));
		p.set_operation(static_cast<proto::events::HistoryOperation>(e.operation));
		p.set_node(e.node);
		p.set_subject(e.subject);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.workspace_handle = p.workspace_handle();
		e.transaction = p.transaction();
		e.phase = static_cast<Event::Phase>(p.phase());
		e.operation = static_cast<HistoryOperation>(p.operation());
		e.node = toast::UID::fromString(p.node());
		e.subject = p.subject();
		return e;
	}
};

TOAST_PROTO_EVENT(WorkspaceHistoryTransaction);

template<>
struct ProtoTraits<HistoryConflict> {
	using Proto = proto::events::HistoryConflict;
	using Event = HistoryConflict;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_id(e.id);
		p.set_node_uid(e.node_uid);
		p.set_node_name(e.node_name);
		p.set_kind(static_cast<proto::events::HistoryConflictKind>(e.kind));
		p.set_field(e.field);
		p.set_value_type(e.value_type);
		p.set_is_array(e.is_array);
		p.set_ref_type(e.ref_type);
		p.set_base_value(e.base_value);
		p.set_current_value(e.current_value);
		p.set_incoming_value(e.incoming_value);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.id = p.id();
		e.node_uid = toast::UID::fromString(p.node_uid());
		e.node_name = p.node_name();
		e.kind = static_cast<HistoryConflictKind>(p.kind());
		e.field = p.field();
		e.value_type = p.value_type();
		e.is_array = p.is_array();
		e.ref_type = p.ref_type();
		e.base_value = p.base_value();
		e.current_value = p.current_value();
		e.incoming_value = p.incoming_value();
		return e;
	}
};

template<>
struct ProtoTraits<WorkspaceHistoryConflicts> {
	using Proto = proto::events::WorkspaceHistoryConflicts;
	using Event = WorkspaceHistoryConflicts;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_workspace_handle(e.workspace_handle);
		p.set_request(e.request);
		p.set_is_merge(e.is_merge);
		p.set_source_revision(e.source_revision);
		for (const auto& conflict : e.conflicts) {
			*p.add_conflicts() = ProtoTraits<HistoryConflict>::toProto(conflict);
		}
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.workspace_handle = p.workspace_handle();
		e.request = p.request();
		e.is_merge = p.is_merge();
		e.source_revision = p.source_revision();
		for (const auto& conflict : p.conflicts()) {
			e.conflicts.push_back(ProtoTraits<HistoryConflict>::fromProto(conflict));
		}
		return e;
	}
};

TOAST_PROTO_EVENT(WorkspaceHistoryConflicts);

template<>
struct ProtoTraits<HistoryConflictResolution> {
	using Proto = proto::events::HistoryConflictResolution;
	using Event = HistoryConflictResolution;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_conflict(e.conflict);
		p.set_choice(static_cast<Proto::Choice>(e.choice));
		p.set_custom_value(e.custom_value);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.conflict = p.conflict();
		e.choice = static_cast<Event::Choice>(p.choice());
		e.custom_value = p.custom_value();
		return e;
	}
};

template<>
struct ProtoTraits<WorkspaceResolveHistoryConflicts> {
	using Proto = proto::events::WorkspaceResolveHistoryConflicts;
	using Event = WorkspaceResolveHistoryConflicts;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_workspace_handle(e.workspace_handle);
		p.set_request(e.request);
		p.set_cancel(e.cancel);
		for (const auto& resolution : e.resolutions) {
			*p.add_resolutions() = ProtoTraits<HistoryConflictResolution>::toProto(resolution);
		}
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.workspace_handle = p.workspace_handle();
		e.request = p.request();
		e.cancel = p.cancel();
		for (const auto& resolution : p.resolutions()) {
			e.resolutions.push_back(ProtoTraits<HistoryConflictResolution>::fromProto(resolution));
		}
		return e;
	}
};

TOAST_PROTO_EVENT(WorkspaceResolveHistoryConflicts);

#undef TOAST_HISTORY_HANDLE_EVENT
#undef TOAST_HISTORY_REVISION_EVENT

}
