/**
 * @file world_events.hpp
 * @author Xein
 * @date 18 May 2026
 *
 * @brief Events that allow client code to interact with the World class
 */

#pragma once
#include "box.hpp"
#include <string>
#include <toast/events/event.hpp>
#include <toast/uid.hpp>

namespace toast {
class Node;
}

namespace event {

struct LoadNode : Event<LoadNode> {
	explicit LoadNode(toast::UID uid) : uid(uid) {}
	toast::UID uid;
};

struct LoadNodeByURI : Event<LoadNodeByURI> {
	explicit LoadNodeByURI(std::string uri) : uri(std::move(uri)) {}
	std::string uri;
};

struct SetWorldRoot : Event<SetWorldRoot> {
	explicit SetWorldRoot(toast::Node& node) : node(&node) {}
	toast::Node* node;
};

struct CacheNode : Event<CacheNode> {
	explicit CacheNode(toast::Node& node) : node(&node) {}
	toast::Node* node;
};

struct DestroyNode : Event<DestroyNode> {
	explicit DestroyNode(toast::Node& node) : node(&node) {}
	toast::Node* node;
};

struct MakeNodeGlobal : Event<MakeNodeGlobal> {
	explicit MakeNodeGlobal(toast::Node& node) : node(&node) {}
	toast::Node* node;
};

struct AttachNode : Event<AttachNode> {
	explicit AttachNode(toast::Node& node, toast::Node& parent) : node(&node), parent(&parent) {}
	toast::Node* node;
	toast::Node* parent;
};

}
