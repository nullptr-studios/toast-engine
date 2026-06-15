/**
 * @file Workspace.hpp
 * @author Xein
 * @date 15 Jun 2026
 *
 * @brief Reduced @c World class for the editor viewport
 *
 * Doesn't multithread anything or handle ticking
 */

#pragma once

#include "node_owner.hpp"
#include <toast/events/listener.hpp>

namespace toast {
class Workspace : public NodeOwner {
public:
	Workspace(std::string_view type, std::string_view name);
	Workspace(std::string_view uri);
	Workspace(UID uid);

	void registerDependency(Node& from, Node& to) override;

	auto requestRuntimeCreation(Node& parent) -> Box<Node> override;

	auto findFrom(const Node& origin, std::string_view query) -> Box<Node> override;

	auto searchFrom(const Node& origin, std::string_view query) -> std::vector<Box<Node>> override;

	void spawnInto(Node& parent, toast::UID prefab) override;

private:
	Box<Node> m_root_node;
	event::Listener listener;

	void eventSubscriptions();
};
}
