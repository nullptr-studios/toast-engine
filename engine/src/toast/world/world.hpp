/**
 * @file world.hpp
 * @author Dante Harper
 * @date 29 Apr 26
 */

#pragma once

// World
// - RootNode
// - GlobalNodes
// - CachedNodes
// - UniqueNodes
// - LoadingNodes
// - DestroyNodes
//
// load - constructor
// init - when an object is fully initized
//     begin - when a object is moved to rootnode or globalnodes
//         onenable
//             tick
//         ondisable
//     end - when an object is moved from rootnode or globalnodes
// destroy - when the object gets added to the destroynodes
// free - deconstructor

#include <memory>
#include <node.hpp>
#include <toast/export.hpp>

namespace toast {
class TOAST_API World {
	friend class Node;
	inline static World* instance = nullptr;

	struct {
		Box<Node> root;
		std::vector<Box<Node>> global;
		std::vector<Box<Node>> cached;
		std::vector<Box<Node>> loading;
		std::vector<Box<Node>> destroy;

		std::vector<Box<Node>> unique;    // this is kinda a memory pool for objects we know only exist one of
	} m;

public:
	static auto create() noexcept -> std::unique_ptr<World>;

	World(const World&) = delete;
	World(World&&) = delete;
	auto operator=(const World&) -> World& = delete;
	auto operator=(World&&) -> World& = delete;

	// private:
	World() = default;

	template<NodeType T = Node>
	static auto find(std::string_view path) -> Box<T>;

	template<NodeType T = Node>
	static auto search(std::string_view path) -> Box<T>;
};

}

#include "world.inl"
