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
		p.set_uid(e.is_uri ? e.uid : toast::UID {0});
		p.set_uri(e.uri);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.parent = toast::UID::fromString(p.parent());
		e.is_uri = p.is_uri();
		e.uid = e.is_uri ? toast::UID::fromString(p.uid()) : toast::UID {0};
		e.uri = p.uri();
		return e;
	}
};

TOAST_PROTO_EVENT(WorkspaceSpawn);

template<>
struct ProtoTraits<WorkspaceRemove> {
	using Proto = proto::events::WorkspaceRemove;
	using Event = WorkspaceRemove;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_target(e.target);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return {toast::UID::fromString(p.target())}; }
};

TOAST_PROTO_EVENT(RequestHierarchyUpdate);

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
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return {toast::UID::fromString(p.target()), p.path()}; }
};

TOAST_PROTO_EVENT(WorkspaceSave);

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
		p.set_parameter(e.parameter);
		p.set_value(e.value);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return {p.parameter(), p.value()}; }
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

}
