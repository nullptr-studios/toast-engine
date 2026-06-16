#include "workspace_events.hpp"

#include <generated/workspace_events.pb.h>
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
		*p.mutable_root() = ProtoTraits<UpdateHierarchyData::HierarchyElement>::toProto(e.root);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		return {ProtoTraits<UpdateHierarchyData::HierarchyElement>::fromProto(p.root())};
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

}
