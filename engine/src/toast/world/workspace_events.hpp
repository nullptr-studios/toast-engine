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

}
