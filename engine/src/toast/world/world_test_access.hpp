#pragma once

#include "world.hpp"

#include <memory>
#include <string_view>
#include <toast/export.hpp>

namespace toast::_detail {

struct TOAST_API WorldTestAccess {
	struct TOAST_API WorldDeleter {
		void operator()(World* world) const noexcept;
	};

	using WorldPtr = std::unique_ptr<World, WorldDeleter>;

	static auto createWorld() -> WorldPtr;

	static auto createNode(World& world, std::string_view name, NodeState state = NodeState::root) -> Box<Node>;

	static void registerDependency(Node& from, Node& to);

	// Test-only: make `node` participate in the given tick stage by attaching a fabricated
	// NodeInfo (the per-instance NodeFunctionTable no longer exists).
	static void addTickStage(Node& node, TickFunctionList stage);

	static auto tickSchedule(World& world) noexcept -> _detail::TickSchedule&;

	static auto dependencyGraph(World& world) noexcept -> World::DependencyGraph&;

	static void computeDependencyGraph(World& world);

	static auto instantiate(World& world, const assets::AssetHandle<assets::Prefab>& file, NodeOwner::InstantiateContext& ctx)
	    -> Box<Node>;

	static auto childrenOf(const Node& node) -> const std::vector<Box<Node>>&;
	static auto isPrefabInterior(const Node& node) -> bool;

	static void initThreadPool();

	static void setWorldRoot(World& world, Node& node);

	static auto
	    spawnSync(World& world, const assets::AssetHandle<assets::Prefab>& file, Node& parent, NodeOwner::InstantiateContext& ctx)
	        -> Box<Node>;

	static void initAssetManager(std::string_view assets_dir, std::string_view cache_dir);

	static void waitForLoads(World& world);

	static void drainLoadQueue(World& world);

	static auto dependencyGraphGraphviz(const World& world) -> std::string;

	static void loadNode(toast::UID uid);
	static auto findCached(std::string_view name) -> Box<Node>;
	static auto findNode(const UID& uid, Node* scope = nullptr) -> Box<Node>;
	static auto findNode(std::string_view path) -> Box<Node>;
	static auto uidPath(const Node& node) -> std::string;
};

}
