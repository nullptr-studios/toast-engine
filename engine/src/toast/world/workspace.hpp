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
class Workspace : public INodeOwner {
public:
	Workspace(std::string_view type, UID handle);
	Workspace(UID uid);
	auto name() -> std::string override;

	void registerDependency(Node& from, Node& to) override;

	auto findFrom(const Node& origin, std::string_view query) -> Box<Node> override;

	auto searchFrom(const Node& origin, std::string_view query) -> std::vector<Box<Node>> override;

private:
	UID m_handle;
	Box<Node> m_root_node;
	event::Listener m_listener;

	void eventSubscriptions();

public:
	void tick() override { }
};
}
