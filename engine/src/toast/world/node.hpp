/**
 * @file node.hpp
 * @author Dante Harper
 * @date 29 Apr 26
 */

#pragma once

#include "toast/events/listener.hpp"
#include "toast/world/box.hpp"
#include "toast/world/registry.hpp"
#include "uuid.hpp"

#include <memory>

// Objects Cannot Be Named
// root
// global

namespace toast {

class TOAST_API Node {
	friend struct _detail::ControlBox;
	template<NodeType T>
	friend class Box;

	struct {
		NodeVTable* v_table;
		Box<Node> box;
		UUID uuid;
		bool enabled;

		std::string name;

		std::unique_ptr<event::Listener> listener;
		std::vector<Box<Node>> hierarchy;
		std::vector<Box<Node>> connections;
	} m;

public:
	Node();

	void name(std::string_view name) noexcept;

	[[nodiscard]]
	auto name() const noexcept -> std::string;

	void enabled(bool state) noexcept;

	[[nodiscard]]
	auto enabled() const noexcept -> bool;

	void destroy();

private:
	[[nodiscard]]
	auto listener() noexcept -> event::Listener&;

	template<NodeType T = Node>
	auto find(std::string_view) -> Box<T>;

	template<NodeType T = Node>
	auto search(std::string_view) -> Box<T>;
};

}

#include "node.inl"
