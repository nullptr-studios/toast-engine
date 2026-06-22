#include "world.hpp"

#include "node_3d.hpp"
#include "workspace_events.hpp"
#include "world_test_access.hpp"

#include <chrono>
#include <sstream>
#include <toast/assets/asset_manager.hpp>
#include <toast/uri_handler.hpp>
#include <toast/assets/assets.hpp>
#include <toast/assets/types.hpp>
#include <toast/thread_pool.hpp>
#include <utility>

namespace toast {

using namespace _detail;

#pragma region NODE_CLUSTER

namespace _detail {
void NodeCluster::earlyTick() {
	for (auto& node : nodes) {
		node->callTick(node->info(), TickFunctionList::early_tick);
	}
}

void NodeCluster::tick() {
	for (auto& node : nodes) {
		node->callTick(node->info(), TickFunctionList::tick);
	}
}

void NodeCluster::postPhysics() {
	for (auto& node : nodes) {
		node->callTick(node->info(), TickFunctionList::post_physics);
	}
}

void NodeCluster::lateTick() {
	for (auto& node : nodes) {
		node->callTick(node->info(), TickFunctionList::late_tick);
	}
}

auto NodeCluster::hasEarlyTick() -> bool {
	for (auto& node : nodes) {
		if (node->info() && node->info()->hasFunction(TickFunctionList::early_tick)) {
			return true;
		}
	}
	return false;
}

auto NodeCluster::hasTick() -> bool {
	for (auto& node : nodes) {
		if (node->info() && node->info()->hasFunction(TickFunctionList::tick)) {
			return true;
		}
	}
	return false;
}

auto NodeCluster::hasPostPhysics() -> bool {
	for (auto& node : nodes) {
		if (node->info() && node->info()->hasFunction(TickFunctionList::post_physics)) {
			return true;
		}
	}
	return false;
}

auto NodeCluster::hasLateTick() -> bool {
	for (auto& node : nodes) {
		if (node->info() && node->info()->hasFunction(TickFunctionList::late_tick)) {
			return true;
		}
	}
	return false;
}
}

#pragma endregion NODE_CLUSTER

World::World() {
	instance = this;

	m.listener.subscribe<event::LoadNode>("load_node", [](event::LoadNode& e) {
		loadNode(e.uid);
		return false;
	});

	m.listener.subscribe<event::LoadNodeByURI>("load_node_uri", [](event::LoadNodeByURI& e) {
		loadNode(e.uri);
		return false;
	});

	m.listener.subscribe<event::SetWorldRoot>("set_world_root", [](event::SetWorldRoot& e) {
		setRoot(*e.node);
		return false;
	});

	m.listener.subscribe<event::CacheNode>("cache_node", [](event::CacheNode& e) {
		cacheNode(*e.node);
		return false;
	});

	m.listener.subscribe<event::DestroyNode>("destroy_node", [](event::DestroyNode& e) {
		destroyNode(*e.node);
		return false;
	});

	m.listener.subscribe<event::MakeNodeGlobal>("make_node_global", [](event::MakeNodeGlobal& e) {
		instance->moveToGlobal(*e.node);
		return false;
	});

	m.listener.subscribe<event::AttachNode>("attach_node", [](event::AttachNode& e) {
		instance->moveToChild(*e.node, *e.parent);
		return false;
	});

	TOAST_INFO("World", "Created world");
}

World::~World() {
	ZoneScoped;
	for (auto& f : m.load_futures) {
		if (f.valid()) {
			f.wait();
		}
	}

	TOAST_INFO("World", "Destroyed world");
}

void World::tick() {
	ZoneScoped;

	drainDestroyQueue();
	drainLoadQueue();
	drainSpawnQueue();

	/**
	 * dispatches one phase: submits each wave to the thread pool and joins before advancing to the next wave;
	 * clusters tick their nodes synchronously within the thread to preserve SCC ordering
	 */
	auto run_phase = [](const std::vector<TickSchedule::Wave>& phase, TickFunctionList func, std::string_view name) {
		ZoneScopedN("World::tick()::function");    // NOLINT
		ZoneNameF("World::tick()::%s", name.data());

		for (const auto& wave : phase) {
			std::vector<std::future<void>> futures;
			futures.reserve(wave.size());

			int count = 1;
			for (const auto& n : wave) {
				ZoneScopedN("World::tick()::function::wave");    // NOLINT
				ZoneNameF("Wave #%i", count++);

				futures.emplace_back(ThreadPool::push([n, func] {
					if (std::holds_alternative<Box<Node>>(n)) {
						auto node = std::get<Box<Node>>(n);
						node->callTick(node->info(), func);
						return;
					}

					// Clusters tick their nodes synchronously to avoid race conditions
					auto cluster = std::get<NodeCluster>(n);
					for (auto& node : cluster.nodes) {
						node->callTick(node->info(), func);
					}
				}));
			}

			{
				ZoneScopedN("Thread Pool semaphore");    // NOLINT
				for (auto& f : futures) {
					f.get();
				}
			}
		}
	};

	run_phase(tick_schedule.early_tick, TickFunctionList::early_tick, "early_tick");
	run_phase(tick_schedule.tick, TickFunctionList::tick, "tick");
	// TODO: physics step goes between tick and post_physics
	run_phase(tick_schedule.post_physics, TickFunctionList::post_physics, "post_physics");
	run_phase(tick_schedule.late_tick, TickFunctionList::late_tick, "late_tick");
}

void World::registerDependency(Node& from, Node& to) {
	if (&from == &to) {
		TOAST_WARN("World", "{} ({}) tried to register a dependency to itself", from.name(), from.uid());
		return;
	}

	// don't store duplicates
	auto& edges = instance->dependency_graph.connections[from];
	if (std::ranges::contains(edges, Box<Node>(to))) {
		return;
	}

	edges.emplace_back(to);
	instance->dependency_graph.inverse_connections[to].emplace_back(from);
	TOAST_TRACE("World", "Added dependency from {} to {}", from.name(), from.uid());
}

void World::loadNode(UID uid) {
	ZoneScoped;
	ZoneNameF("World::loadNode(%s)", uid.get().c_str());
	TOAST_INFO("World", "Loading node {} from file", uid);

	// Load stages:
	//		1: get the node_file
	//		2: dispatch 1:
	//				- control block allocation
	//				- deserialize
	//				- preInit()
	//		3: data structure building:
	//				- build tree structure
	//		4: dispatch 2:
	//				- init()

	auto future = std::async(std::launch::async, [uid]() {
#ifdef TRACY_ENABLE
		tracy::SetThreadName("World::loadNode worker");
#endif
		ZoneScoped;    // NOLINT
		ZoneNameF("World::loadNode(%s)::async", uid.get().c_str());

		auto node_file = assets::load<assets::Prefab>(uid);
		if (not node_file.hasValue()) {
			TOAST_ERROR("World", "Couldn't load Node {}", uid);
			return;
		}

		INodeOwner::InstantiateContext ctx;
		ctx.resolver = [](toast::UID id) { return assets::load<assets::Prefab>(id); };
		Box<Node> root = instance->instantiate(node_file, ctx);

		if (not root.exists()) {
			TOAST_ERROR("World", "Failed to instantiate Node {}", uid);
			return;
		}

		root->propagateCallTick(root->info(), TickFunctionList::init);
		TOAST_TRACE("World", "Node {} ({}) finished loading", root->name(), root->uid());

		std::scoped_lock lock(instance->m.load_mutex);
		instance->trees.load_queue.emplace_back(std::move(root));
	});

	std::scoped_lock lock(instance->m.load_mutex);
	std::erase_if(instance->m.load_futures, [](std::future<void>& f) {
		return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
	});
	instance->m.load_futures.emplace_back(std::move(future));
}

void World::loadNode(std::string_view uri) {
	// just reroute to the actual loadNode() implementation
	auto id = assets::resolveURI(uri);

#ifndef NDEBUG
	if (not id.has_value()) {
		TOAST_WARN("World", "Couldn't load Node {}", uri);
		return;
	}
#endif

	loadNode(*id);
}

void World::drainLoadQueue() {
	std::vector<Box<Node>> loaded;
	{
		std::scoped_lock lock(m.load_mutex);
		if (trees.load_queue.empty()) {
			return;
		}
		std::swap(loaded, trees.load_queue);
	}

	ZoneScoped;

	// Freshly loaded trees go to the cached list and are ready to be activated
	for (auto& root : loaded) {
		root->changeNodeState(NodeState::cached);
		TOAST_TRACE("World", "Node {} ({}) moved to cache", root->name(), root->uid());
		trees.cached.emplace_back(std::move(root));
	}
}

void World::spawn(UID prefab, Node& parent) {
	ZoneScoped;
	TOAST_INFO("World", "Spawning prefab {} under {}", prefab, parent.name());

	Box<Node> parent_box = parent.box();
	auto future = std::async(std::launch::async, [prefab, parent_box]() {
#ifdef TRACY_ENABLE
		tracy::SetThreadName("World::spawn worker");
#endif
		ZoneScoped;    // NOLINT

		auto file = assets::load<assets::Prefab>(prefab);
		if (not file.hasValue()) {
			TOAST_ERROR("World", "Couldn't load prefab {} to spawn", prefab);
			return;
		}

		INodeOwner::InstantiateContext ctx;
		ctx.resolver = [](toast::UID id) { return assets::load<assets::Prefab>(id); };
		Box<Node> root = instance->instantiate(file, ctx);
		if (not root.exists()) {
			TOAST_ERROR("World", "Failed to instantiate prefab {} to spawn", prefab);
			return;
		}

		// spawned instances need a unique UID within the parent's namespace
		// even if two copies of the same prefab are spawned concurrently
		generateUid(*root);
		root->propagateCallTick(root->info(), TickFunctionList::init);
		root->changeNodeState(NodeState::cached);

		std::scoped_lock lock(instance->m.load_mutex);
		instance->m.spawn_queue.emplace_back(std::move(root), parent_box);
	});

	std::scoped_lock lock(instance->m.load_mutex);
	std::erase_if(instance->m.load_futures, [](std::future<void>& f) {
		return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
	});
	instance->m.load_futures.emplace_back(std::move(future));
}

void World::drainSpawnQueue() {
	std::vector<std::pair<Box<Node>, Box<Node>>> ready;
	{
		std::scoped_lock lock(m.load_mutex);
		if (m.spawn_queue.empty()) {
			return;
		}
		std::swap(ready, m.spawn_queue);
	}

	ZoneScoped;

	for (auto& [root, parent] : ready) {
		if (not parent.exists() || parent->m_state == NodeState::destroy) {
			TOAST_WARN("World", "Spawn target for {} no longer exists; dropping the spawn", root->name());
			continue;
		}

		// Keep the placement UID unique within the parents namespace
		while (findNode(root->uid(), &*parent).exists()) {
			generateUid(*root);
		}

		moveToChild(*root, *parent);
	}
}

auto World::findNode(const UID& uid, Node* scope) -> Box<Node> {
	ZoneScoped;

	Node* start = scope;
	if (not start) {
		if (not instance->trees.root.exists()) {
			return {};
		}
		start = &*instance->trees.root;
	}

	// Depth-first, but a nested instance root is opaque
	auto dfs = [&uid](this auto&& self, Node& n, bool is_scope_root) -> Box<Node> {
		if (n.uid().data() == uid.data()) {
			return n.box();
		}
		if (not is_scope_root && n.isInstanceRoot()) {
			return {};
		}
		for (auto& child : n.m_children) {
			if (auto found = self(*child, false)) {
				return found;
			}
		}
		return {};
	};

	return dfs(*start, true);
}

auto World::findNode(std::string_view path) -> Box<Node> {
	Box<Node> current;
	Node* scope = nullptr;    // first segment is resolved from the world root
	size_t start = 0;

	while (true) {
		size_t slash = path.find('/', start);
		std::string_view seg = path.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
		if (seg.empty()) {
			return {};
		}

		current = findNode(UID(UID::fromString(seg)), scope);
		if (not current.exists()) {
			return {};
		}
		scope = &*current;    // the next segment is scoped to this instance's interior

		if (slash == std::string_view::npos) {
			break;
		}
		start = slash + 1;
	}

	return current;
}

auto World::uidPath(const Node& node) -> std::string {
	const Node* root = instance->trees.root.exists() ? &*instance->trees.root : nullptr;

	std::vector<std::string> parts;
	for (const Node* n = &node; n != nullptr;) {
		if (n->isInstanceRoot()) {
			parts.push_back(n->uid().get());
		}
		if (n == root) {
			break;
		}
		Box<Node> parent = n->parentInternal();
		n = parent.exists() ? &*parent : nullptr;
	}
	std::ranges::reverse(parts);

	std::string result;
	for (size_t i = 0; i < parts.size(); ++i) {
		if (i != 0) {
			result += '/';
		}
		result += parts[i];
	}
	return result;
}

namespace {

auto looksLikeUid(std::string_view seg) -> bool {
	if (seg.size() != 11) {
		return false;
	}
	for (char c : seg) {
		bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
		if (not ok) {
			return false;
		}
	}
	return true;
}

enum class QueryRoot : uint8_t {
	self,
	prefab_root,
	world_root,
	global
};

struct ParsedQuery {
	QueryRoot root = QueryRoot::self;
	std::vector<std::string_view> segments;    // path after the namespace keyword
	bool valid = true;
};

auto parseQuery(std::string_view q) -> ParsedQuery {
	ParsedQuery out;

	// only node:// is accepted, any other URI scheme (asset://) is an error
	if (auto pos = q.find("://"); pos != std::string_view::npos) {
		if (q.substr(0, pos) != "node") {
			out.valid = false;
			return out;
		}
		q.remove_prefix(pos + 3);
	}

	std::vector<std::string_view> parts;
	size_t start = 0;
	while (start <= q.size()) {
		size_t slash = q.find('/', start);
		size_t len = (slash == std::string_view::npos) ? std::string_view::npos : slash - start;
		std::string_view seg = q.substr(start, len);
		if (not seg.empty()) {
			parts.push_back(seg);
		}
		if (slash == std::string_view::npos) {
			break;
		}
		start = slash + 1;
	}

	if (parts.empty()) {
		out.valid = false;
		return out;
	}

	size_t first = 0;
	if (parts[0] == "root") {
		out.root = QueryRoot::prefab_root;
		first = 1;
	} else if (parts[0] == "world_root") {
		out.root = QueryRoot::world_root;
		first = 1;
	} else if (parts[0] == "global") {
		out.root = QueryRoot::global;
		first = 1;
	}

	out.segments.assign(parts.begin() + first, parts.end());
	return out;
}

}

auto World::findScoped(Node& scope, std::string_view seg, bool by_uid) -> Box<Node> {
	uint64_t target = by_uid ? UID::fromString(seg) : 0;

	auto dfs = [&](this auto&& self, Node& n, bool is_scope_root) -> Box<Node> {
		bool match = by_uid ? (n.uid().data() == target) : (n.name() == seg);
		if (match) {
			return n.box();
		}
		// Instances are opaque
		if (not is_scope_root && n.isInstanceRoot()) {
			return {};
		}
		for (auto& child : n.m_children) {
			if (auto found = self(*child, false)) {
				return found;
			}
		}
		return {};
	};

	return dfs(scope, true);
}

void World::searchScoped(Node& scope, std::string_view seg, bool by_uid, std::vector<Box<Node>>& out) {
	uint64_t target = by_uid ? UID::fromString(seg) : 0;

	auto dfs = [&](this auto&& self, Node& n) -> void {
		if (by_uid ? (n.uid().data() == target) : (n.name() == seg)) {
			out.emplace_back(n.box());
		}
		// search() is the general form, it crosses into prefab interiors
		for (auto& child : n.m_children) {
			self(*child);
		}
	};

	dfs(scope);
}

auto World::findFrom(const Node& origin, std::string_view query) -> Box<Node> {
	ZoneScoped;

	ParsedQuery pq = parseQuery(query);
	if (not pq.valid) {
		TOAST_ERROR("World", "Invalid node query \"{}\"", query);
		return {};
	}

	// Resolve the starting scope from the namespace keyword
	std::vector<Node*> origins;
	switch (pq.root) {
		case QueryRoot::self: origins.push_back(const_cast<Node*>(&origin)); break;
		case QueryRoot::prefab_root: origins.push_back(&*const_cast<Node&>(origin).root()); break;
		case QueryRoot::world_root:
			if (trees.root.exists()) {
				origins.push_back(&*trees.root);
			}
			break;
		case QueryRoot::global:
			if (pq.segments.empty()) {
				TOAST_ERROR("World", "find(\"global\") is ambiguous; use search(\"global\") to list global nodes");
				return {};
			}
			for (auto& g : trees.global) {
				origins.push_back(&*g);
			}
			break;
	}

	// A bare keyword resolves to the scope node itself
	if (pq.segments.empty()) {
		return origins.empty() ? Box<Node> {} : origins.front()->box();
	}

	bool by_uid = looksLikeUid(pq.segments.front());

	for (Node* start : origins) {
		Node* scope = start;
		Box<Node> current;
		bool ok = true;
		for (std::string_view seg : pq.segments) {
			current = findScoped(*scope, seg, by_uid);
			if (not current.exists()) {
				ok = false;
				break;
			}
			scope = &*current;
		}
		if (ok) {
			return current;    // find() returns the first match
		}
	}

	return {};
}

auto World::searchFrom(const Node& origin, std::string_view query) -> std::vector<Box<Node>> {
	ZoneScoped;

	ParsedQuery pq = parseQuery(query);
	if (not pq.valid) {
		TOAST_ERROR("World", "Invalid node query \"{}\"", query);
		return {};
	}

	std::vector<Box<Node>> out;

	// "global" with no path lists the world's global nodes
	if (pq.root == QueryRoot::global && pq.segments.empty()) {
		out.reserve(trees.global.size());
		for (auto& g : trees.global) {
			out.emplace_back(g);
		}
		return out;
	}

	std::vector<Node*> origins;
	switch (pq.root) {
		case QueryRoot::self: origins.push_back(const_cast<Node*>(&origin)); break;
		case QueryRoot::prefab_root: origins.push_back(&*const_cast<Node&>(origin).root()); break;
		case QueryRoot::world_root:
			if (trees.root.exists()) {
				origins.push_back(&*trees.root);
			}
			break;
		case QueryRoot::global:
			for (auto& g : trees.global) {
				origins.push_back(&*g);
			}
			break;
	}

	if (pq.segments.empty()) {
		for (Node* o : origins) {
			out.emplace_back(o->box());
		}
		return out;
	}

	bool by_uid = looksLikeUid(pq.segments.front());

	for (Node* start : origins) {
		Node* scope = start;
		bool ok = true;
		for (size_t i = 0; i + 1 < pq.segments.size(); ++i) {
			Box<Node> step = findScoped(*scope, pq.segments[i], by_uid);
			if (not step.exists()) {
				ok = false;
				break;
			}
			scope = &*step;
		}
		if (ok) {
			searchScoped(*scope, pq.segments.back(), by_uid, out);
		}
	}

	return out;
}

auto World::setRoot(Node& node) -> Box<Node> {
	return instance->swapRoot(node);
}

auto World::cacheNode(Node& node) -> Box<Node> {
	return instance->moveToCached(node);
}

auto World::findCached(std::string_view name) -> Box<Node> {
	ZoneScoped;

	for (const auto& node : instance->trees.cached) {
		if (node->name() == name) {
			return node;
		}
	}

	TOAST_TRACE("World", "Node {} not found in cached", name);
	return {};
}

auto World::graphviz() -> std::string {
	return instance->dependencyGraphGraphviz();
}

void World::destroyNode(Node& node) {
	ZoneScoped;
	ZoneNameF("World::destroyNode(%s)", node.name().data());

	if (node.m_state != NodeState::cached) {
		TOAST_WARN("World", "Only cached nodes can be destroyed, {} ({}) is still in use", node.name(), node.uid());
		return;
	}

	std::erase(instance->trees.cached, node.box());
	node.changeNodeState(NodeState::destroy);
	instance->trees.destroy_queue.emplace_back(node.box());
	TOAST_TRACE("World", "Queued node {} ({}) for destruction", node.name(), node.uid());
}

void World::drainDestroyQueue() {
	std::vector<Box<Node>> doomed;
	std::swap(doomed, trees.destroy_queue);

	if (!doomed.empty()) {
		ZoneScopedN("World::drainDestroyQueue()");

		/**
		 * order matters: destroy() callbacks first (tree is still intact), then sever Box<> edges, then free;
		 * this way destroy() can safely read its own children and parent
		 */
		for (auto& root : doomed) {
			root->propagateCallTick(root->info(), TickFunctionList::destroy);
		}

		// Collect every node of every doomed tree
		std::vector<Node*> victims;
		auto collect = [&victims](this auto&& self, Node& node) -> void {
			victims.push_back(&node);
			for (auto& child : node.m_children) {
				self(*child);
			}
		};
		for (auto& root : doomed) {
			collect(*root);
		}
		doomed.clear();    // drop our own references before tearing the trees down

		std::unordered_set<const Node*> victim_set(victims.begin(), victims.end());

		// Scrub every edge that involves a victim from the dependency graph
		auto scrub = [&](std::unordered_map<Box<Node>, std::vector<Box<Node>>>& connections) {
			for (Node* victim : victims) {
				connections.erase(victim->box());
			}
			for (auto& [node, edges] : connections) {
				std::erase_if(edges, [&](const Box<Node>& edge) { return edge.exists() && victim_set.contains(&*edge); });
			}
		};
		scrub(dependency_graph.connections);
		scrub(dependency_graph.inverse_connections);

		// Detach the tree structure so no victim holds a Box to another
		for (Node* victim : victims) {
			victim->m_parent = {};
			victim->m_children.clear();
			victim->m_listener.reset();
		}

		// Free the nodes
		{
			std::scoped_lock lock(nodes_mutex);
			for (Node* victim : victims) {
				ControlBox* control = ControlBox::get(victim);
				const NodeInfo* info = victim->info();
				TOAST_TRACE("World", "Destroying node {} ({})", victim->name(), victim->uid());

				if (info && info->destroy) {
					info->destroy(victim);
				} else {
					delete victim;
				}
				releaseNode(*control);
			}
		}

		computeDependencyGraph();
	}

	reapTombstones();
}

void World::markNode3DDependantsDirty(const Box<Node>& node) noexcept {
	if (!instance) {
		return;
	}

	auto it = instance->dependency_graph.inverse_connections.find(node);
	if (it != instance->dependency_graph.inverse_connections.end()) {
		for (auto& dependent : it->second) {
			if (auto node3d = dependent.as<Node3D>()) {
				node3d->m_dirty_world = true;
			}
		}
	}
}

void World::computeDependencyGraph() {
	ZoneScoped;
	TOAST_TRACE("World", "Computing dependency graph");

	std::vector<std::vector<Box<Node>>> subgraphs;
	{
		// Loader threads insert into m.nodes concurrently
		std::scoped_lock lock(nodes_mutex);

		// Guarantee every existing node exists in the subgraph
		forEachNode([&](const ControlBox& node) {
			if (!node.node || node.node->m_state == NodeState::destroy) {
				return;
			}
			dependency_graph.connections[node.node->box()];
			dependency_graph.inverse_connections[node.node->box()];
		});

		subgraphs = subgraphSeparation();
	}

	// We are gonna remove nodes that will not be able to get ticked because:
	//		1) they don't have any tick functions
	//		2) they are not in an active state (cached, mid-load, etc. don't tick)
	for (auto& g : subgraphs) {
		std::erase_if(g, [](const auto& n) { return not n->info() || not n->info()->hasFunction(TickFunctionList::tick_mask); });

		std::erase_if(g, [](const auto& n) { return n->m_state != NodeState::root && n->m_state != NodeState::global; });
	}

	// Drop subgraphs that became entirely empty
	std::erase_if(subgraphs, [](const auto& g) { return g.empty(); });

	auto result = tarjanAlgorithm(subgraphs);

	auto waves = assignWaves(result);
	auto ts = optimizeWaves(waves);
	tick_schedule = std::move(ts);
	TOAST_TRACE(
	    "World",
	    "Dependency graph: early={} tick={} post_physics={} late={} waves",
	    tick_schedule.early_tick.size(),
	    tick_schedule.tick.size(),
	    tick_schedule.post_physics.size(),
	    tick_schedule.late_tick.size()
	);
}

auto World::subgraphSeparation() -> std::vector<std::vector<Box<Node>>> {
	ZoneScoped;

	using NodeGraph = std::vector<Box<Node>>;
	std::unordered_set<Box<Node>> visited;
	std::vector<NodeGraph> subgraphs;

	forEachNode([&](const ControlBox& start_cn) {
		if (!start_cn.node || start_cn.node->m_state == NodeState::destroy) {
			return;
		}
		auto start_node = start_cn.node->box();
		if (visited.contains(start_node)) {
			return;
		}

		ZoneScoped;

		NodeGraph component;
		std::queue<Box<Node>> queue;

		queue.push(start_node);
		visited.insert(start_node);

		while (!queue.empty()) {
			ZoneScoped;

			auto current = queue.front();
			queue.pop();
			component.push_back(current);

			// Walk forward edges
			if (auto it = dependency_graph.connections.find(current); it != dependency_graph.connections.end()) {
				for (const auto& neighbor : it->second) {
					if (visited.contains(neighbor)) {
						continue;
					}
					visited.insert(neighbor);
					queue.push(neighbor);
				}
			}

			// Walk backward edges
			if (auto it = dependency_graph.inverse_connections.find(current); it != dependency_graph.inverse_connections.end()) {
				for (const auto& neighbor : it->second) {
					if (visited.contains(neighbor)) {
						continue;
					}
					visited.insert(neighbor);
					queue.push(neighbor);
				}
			}
		}

		NodeGraph subgraph;
		subgraph.reserve(component.size());
		subgraph.assign(std::make_move_iterator(component.begin()), std::make_move_iterator(component.end()));
		subgraphs.push_back(std::move(subgraph));
	});

	return subgraphs;
}

/**
 * produces SCCs in reverse topological order so assignWaves iterates in the right direction;
 * SCCs with more than one node indicate a dependency cycle and are bundled into a NodeCluster
 */
auto World::tarjanAlgorithm(const std::vector<std::vector<Box<Node>>>& input_subgraphs) -> std::vector<TickSchedule::Wave> {
	ZoneScoped;

	// SearchContext tracks the state of the DFS traversal for Tarjan's algorithm.
	struct SearchContext {
		std::unordered_map<Box<Node>, int> index;
		std::unordered_map<Box<Node>, int> low_link;
		std::unordered_map<Box<Node>, bool> on_stack;
		std::stack<Box<Node>> stack;
		int counter = 0;
		std::vector<std::vector<Box<Node>>> sccs;    // Components in reverse topological order
	};

	                                               // clang-format off
	// We start with a list of nodes per subgraph that survived the initial pruning
	std::vector<TickSchedule::Wave> processed_subgraphs;
	processed_subgraphs.reserve(input_subgraphs.size());
	for (const auto& subgraph_nodes : input_subgraphs) {
		TickSchedule::Wave wave;
		wave.reserve(subgraph_nodes.size());
		for (const auto& node : subgraph_nodes) {
			wave.emplace_back(node);
		}
		processed_subgraphs.emplace_back(std::move(wave));
	}
	// clang-format on

	std::vector<TickSchedule::Wave> result;

	for (const auto& current_subgraph : processed_subgraphs) {
		ZoneScoped;

		// Create a lookup set to efficiently filter out neighbors that aren't part of this subgraph
		std::unordered_set<Box<Node>> nodes_in_subgraph;
		for (const auto& variant : current_subgraph) {
			nodes_in_subgraph.insert(std::get<Box<Node>>(variant));
		}

		SearchContext search_context;

		// strong_connect is a recursive DFS function that finds SCCs
		// An SCC is a group of nodes where every node is reachable from every other node in the group
		// In our dependency graph, an SCC with >1 node indicates a circular dependency
		std::function<void(const Box<Node>&)> strong_connect = [&](const Box<Node>& current_node) {
			ZoneScopedN("strong_connect");    // NOLINT

			search_context.index[current_node] = search_context.low_link[current_node] = search_context.counter++;
			search_context.stack.push(current_node);
			search_context.on_stack[current_node] = true;

			// Explore neighbors
			if (auto it = dependency_graph.connections.find(current_node); it != dependency_graph.connections.end()) {
				for (const auto& neighbor_node : it->second) {
					// We only care about dependencies between nodes that are actually being ticked
					if (!nodes_in_subgraph.contains(neighbor_node)) {
						continue;
					}

					if (!search_context.index.contains(neighbor_node)) {
						// Neighbor hasn't been visited yet; recurse
						strong_connect(neighbor_node);
						search_context.low_link[current_node] =
						    std::min(search_context.low_link[current_node], search_context.low_link[neighbor_node]);
					} else if (search_context.on_stack[neighbor_node]) {
						// Neighbor is on the stack, meaning we've found a cycle
						search_context.low_link[current_node] =
						    std::min(search_context.low_link[current_node], search_context.index[neighbor_node]);
					}
				}
			}

			// If current_node is the root of an SCC, pop the stack to extract the full component
			if (search_context.low_link[current_node] == search_context.index[current_node]) {
				std::vector<Box<Node>> scc;
				while (true) {
					auto popped_node = search_context.stack.top();
					search_context.stack.pop();
					search_context.on_stack[popped_node] = false;
					scc.push_back(popped_node);
					if (popped_node == current_node) {
						break;
					}
				}
				search_context.sccs.emplace_back(std::move(scc));
			}
		};

		for (const auto& variant : current_subgraph) {
			const auto& node = std::get<Box<Node>>(variant);
			if (!search_context.index.contains(node)) {
				strong_connect(node);
			}
		}

		// Convert SCCs into a wave
		TickSchedule::Wave sorted_wave;
		sorted_wave.reserve(search_context.sccs.size());

		for (auto& scc : search_context.sccs) {
			if (scc.size() == 1) {
				sorted_wave.emplace_back(scc[0]);
			} else {
				// Group all nodes in the cycle into a NodeCluster
				// This ensures they are ticked as a single atomic unit to avoid race conditions
				TOAST_TRACE("World", "Dependency cycle detected ({} nodes) -> grouping into NodeCluster", scc.size());
				sorted_wave.emplace_back(NodeCluster(scc));
			}
		}
		result.emplace_back(std::move(sorted_wave));
	}

	return result;
}

/**
 * assigns each item a wave index equal to max(predecessor wave) + 1;
 * items with no predecessors land on wave 0 and run fully in parallel
 */
auto World::assignWaves(const std::vector<TickSchedule::Wave>& subgraphs) -> std::vector<TickSchedule::Wave> {
	// First, map every node back to its containing item
	// This allows us to treat clusters as a single scheduling unit
	struct ItemLocation {
		int subgraph_idx;
		int item_idx;
	};

	ZoneScoped;

	std::unordered_map<Box<Node>, ItemLocation> node_to_location;

	for (int subgraph_idx = 0; std::cmp_less(subgraph_idx, subgraphs.size()); ++subgraph_idx) {
		for (int item_idx = 0; std::cmp_less(item_idx, subgraphs[subgraph_idx].size()); ++item_idx) {
			std::visit(
			    [&](const auto& item) {
				    using T = std::decay_t<decltype(item)>;
				    if constexpr (std::is_same_v<T, Box<Node>>) {
					    node_to_location[item] = {subgraph_idx, item_idx};
				    } else {
					    // NodeCluster
					    for (const auto& node : item.nodes) {
						    node_to_location[node] = {subgraph_idx, item_idx};
					    }
				    }
			    },
			    subgraphs[subgraph_idx][item_idx]
			);
		}
	}

	// Prepare wave levels for every item in every subgraph
	std::vector<std::vector<int>> item_wave_levels(subgraphs.size());
	for (int subgraph_idx = 0; std::cmp_less(subgraph_idx, subgraphs.size()); ++subgraph_idx) {
		item_wave_levels[subgraph_idx].assign(subgraphs[subgraph_idx].size(), 0);
	}

	// Calculate the wave level for each item
	for (int subgraph_idx = 0; std::cmp_less(subgraph_idx, subgraphs.size()); ++subgraph_idx) {
		ZoneScopedN("Calculate wave level");

		// Iterating in reverse because Tarjan's SCCs are in reverse topological order
		for (int item_idx = (int)subgraphs[subgraph_idx].size() - 1; item_idx >= 0; --item_idx) {
			// Collect all physical nodes in this scheduling item
			std::vector<Box<Node>> item_nodes;
			std::visit(
			    [&](const auto& item) {
				    using T = std::decay_t<decltype(item)>;
				    if constexpr (std::is_same_v<T, Box<Node>>) {
					    item_nodes.push_back(item);
				    } else {
					    item_nodes.insert(item_nodes.end(), item.nodes.begin(), item.nodes.end());
				    }
			    },
			    subgraphs[subgraph_idx][item_idx]
			);

			// A node must be scheduled in a wave strictly higher than all of its predecessors
			int max_predecessor_wave = -1;
			for (const auto& node : item_nodes) {
				auto it = dependency_graph.inverse_connections.find(node);
				if (it == dependency_graph.inverse_connections.end()) {
					continue;
				}

				for (const auto& predecessor : it->second) {
					auto loc_it = node_to_location.find(predecessor);
					if (loc_it == node_to_location.end()) {
						continue;    // Predecessor was pruned
					}

					auto [pred_subgraph_idx, pred_item_idx] = loc_it->second;
					// We only care about dependencies within the same subgraph island
					if (pred_subgraph_idx == subgraph_idx && pred_item_idx != item_idx) {
						max_predecessor_wave = std::max(max_predecessor_wave, item_wave_levels[pred_subgraph_idx][pred_item_idx]);
					}
				}
			}
			item_wave_levels[subgraph_idx][item_idx] = max_predecessor_wave + 1;
		}
	}

	// Find the global maximum wave index to determine how many execution buckets we need
	int global_max_wave = 0;
	for (const auto& levels : item_wave_levels) {
		for (int level : levels) {
			global_max_wave = std::max(global_max_wave, level);
		}
	}

	// Group items into waves
	std::vector<TickSchedule::Wave> buckets(global_max_wave + 1);

	for (int subgraph_idx = 0; std::cmp_less(subgraph_idx, subgraphs.size()); ++subgraph_idx) {
		for (int item_idx = 0; std::cmp_less(item_idx, subgraphs[subgraph_idx].size()); ++item_idx) {
			buckets[item_wave_levels[subgraph_idx][item_idx]].emplace_back(subgraphs[subgraph_idx][item_idx]);
		}
	}

	return buckets;
}

/**
 * prunes items that don't implement the relevant tick function for each phase,
 * then bakes the final wave index into Node::m_wave for O(1) lookup at dispatch time
 */
auto World::optimizeWaves(const std::vector<TickSchedule::Wave>& waves) -> TickSchedule {
	TickSchedule schedule = {
	  .early_tick = waves,
	  .tick = waves,
	  .post_physics = waves,
	  .late_tick = waves,
	};

	ZoneScoped;

	/**
	 * filter_and_assign_wave removes nodes from a wave if they don't have the relevant
	 * tick function for that phase, and records the wave index into the node's metadata
	 */
	auto filter_and_assign_wave =
	    [](std::vector<TickSchedule::Wave>& phase_waves, int wave_meta_index, auto&& has_function_checker) {
		    // Remove items that don't participate in this phase
		    for (auto& wave : phase_waves) {
			    std::erase_if(wave, [&](std::variant<Box<Node>, NodeCluster>& item) {
				    if (std::holds_alternative<Box<Node>>(item)) {
					    auto node = std::get<Box<Node>>(item);
					    return !has_function_checker(node);
				    }

				    const auto& cluster = std::get<NodeCluster>(item);
				    return !std::ranges::any_of(cluster.nodes, [&](const auto& node) { return has_function_checker(node); });
			    });
		    }

		    // Drop waves that became empty after filtering
		    std::erase_if(phase_waves, [](const auto& wave) { return wave.empty(); });

		    // Assign the resulting wave index to all surviving nodes
		    for (int wave_idx = 0; std::cmp_less(wave_idx, phase_waves.size()); ++wave_idx) {
			    for (auto& item : phase_waves[wave_idx]) {
				    std::visit(
				        [&](auto& value) {
					        using T = std::decay_t<decltype(value)>;
					        if constexpr (std::is_same_v<T, Box<Node>>) {
						        value->m_wave[wave_meta_index] = wave_idx;
					        } else {
						        for (auto node : value.nodes) {
							        node->m_wave[wave_meta_index] = wave_idx;
						        }
					        }
				        },
				        item
				    );
			    }
		    }
	    };

	filter_and_assign_wave(schedule.early_tick, 0, [](auto n) {
		return n->info() && n->info()->hasFunction(TickFunctionList::early_tick);
	});
	filter_and_assign_wave(schedule.tick, 1, [](auto n) { return n->info() && n->info()->hasFunction(TickFunctionList::tick); });
	filter_and_assign_wave(schedule.post_physics, 2, [](auto n) {
		return n->info() && n->info()->hasFunction(TickFunctionList::post_physics);
	});
	filter_and_assign_wave(schedule.late_tick, 3, [](auto n) {
		return n->info() && n->info()->hasFunction(TickFunctionList::late_tick);
	});

	return schedule;
}

auto World::swapRoot(Node& node) -> Box<Node> {
	ZoneScoped;
	ZoneNameF("World::swapRoot() [to %s]", node.name().data());

	if (node.m_type != NodeType::root) {
		TOAST_WARN("World", "You can only swap roots with a root node");
		return {};
	}

	// The world may be empty; in that case the node is simply promoted
	auto root_node = trees.root;

	switch (node.m_state) {
		case NodeState::root: TOAST_WARN("World", "Attempted to swap root with a node already in root"); return {};
		case NodeState::destroy:
		case NodeState::loading:
		case NodeState::null:
			TOAST_WARN("World", "Attempted to swap root with a not initialized node or one that is scheduled for destruction");
			return {};
		case NodeState::cached:
			if (root_node.exists()) {
				std::ranges::replace(trees.cached, node.box(), root_node);
				root_node->m_type = NodeType::root;    // world_root only exists in root and global
				root_node->changeNodeState(NodeState::cached);
				root_node->propagateCallTick(root_node->info(), TickFunctionList::end);
				root_node->enabled(false);
			} else {
				std::erase(trees.cached, node.box());
			}
			break;
		case NodeState::global:
			if (root_node.exists()) {
				std::ranges::replace(trees.global, node.box(), root_node);
				root_node->changeNodeState(NodeState::global);
			} else {
				std::erase(trees.global, node.box());
			}
			break;
	}

	node.m_type = NodeType::world_root;
	node.changeNodeState(NodeState::root);
	trees.root = node.box();

	computeDependencyGraph();

	node.propagateCallTick(node.info(), TickFunctionList::begin);
	node.enabled(true);
	event::send<event::RequestHierarchyUpdate>();    // TODO: should the world send this?
	TOAST_INFO("World", "Swapped root to {} ({})", node.name(), node.uid());

	return root_node;
}

auto World::moveToCached(Node& node) -> Box<Node> {
	ZoneScoped;
	ZoneNameF("World::moveToCached() [%s]", node.name().data());

	switch (node.m_state) {
		case NodeState::cached: TOAST_WARN("World", "Tried to move to cache a node already on cache"); return {};
		case NodeState::destroy:
		case NodeState::null:
		case NodeState::loading:
			TOAST_WARN("World", "Tried to move to cache a node that is not initialized or scheduled for destruction");
			return {};
		case NodeState::root:
			if (node.m_type == NodeType::world_root) {
				// Caching the active root leaves the world empty
				trees.root = {};
			} else {
				std::erase(node.parent()->m_children, node.box());
				node.m_parent = {};
			}
			break;
		case NodeState::global:
			if (node.m_type == NodeType::world_root) {
				std::erase(trees.global, node.box());
			} else {
				std::erase(node.parent()->m_children, node.box());
				node.m_parent = {};
			}
			break;
	}

	node.changeNodeState(NodeState::cached);
	node.enabled(false);
	node.propagateCallTick(node.info(), TickFunctionList::end);

	// ensure there's only root nodes or children nodes, no world_root
	// world_root only exists in global and root
	if (node.m_type == NodeType::world_root) {
		node.m_type = NodeType::root;
	}

	trees.cached.emplace_back(node.box());

	computeDependencyGraph();
	TOAST_TRACE("World", "Node {} ({}) moved to cache", node.name(), node.uid());
	return node.box();
}

auto World::moveToGlobal(Node& node) -> Box<Node> {
	switch (node.m_state) {
		case NodeState::root: TOAST_WARN("World", "Tried to move root to cached, consider using swapRoot() instead"); return {};
		case NodeState::global: TOAST_WARN("World", "Tried to move to global a Node that is already in global"); return {};
		case NodeState::destroy:
		case NodeState::null:
		case NodeState::loading:
			TOAST_WARN("World", "Tried to move to cache a node that is not initialized or scheduled for destruction");
			return {};
		case NodeState::cached: break;
	}

	if (node.m_type == NodeType::child) {
		TOAST_WARN("World", "Only Root nodes can be moved to global, {} is a children node", node.name());
		return {};
	}

	std::erase(trees.cached, node.box());

	node.m_type = NodeType::world_root;
	node.changeNodeState(NodeState::global);

	computeDependencyGraph();

	node.propagateCallTick(node.info(), TickFunctionList::begin);
	node.enabled(true);
	trees.global.emplace_back(node.box());
	TOAST_TRACE("World", "Node {} ({}) moved to global", node.name(), node.uid());
	return node.box();
}

auto World::moveToChild(Node& node, Node& parent) -> Box<Node> {
	if (parent.m_state != NodeState::root) {
		TOAST_WARN("World", "You can only move a node into one that is on the root");
		return {};
	}

	switch (node.m_state) {
		case NodeState::null:
		case NodeState::loading:
		case NodeState::destroy:
			TOAST_WARN("World", "Tried to move to cache a node that is not initialized or scheduled for destruction");
			return {};
		case NodeState::root:
			if (node.m_type == NodeType::world_root) {
				TOAST_WARN("World", "Tried to move root to cached, consider using swapRoot() instead");
				return {};
			}
			break;
		case NodeState::global:
		case NodeState::cached: break;
	}

	bool run_begin = false;

	if (node.parent().exists()) {
		std::erase(node.parent()->m_children, node.box());
	} else {
		if (node.m_state == NodeState::global) {
			std::erase(trees.global, node.box());
		} else if (node.m_state == NodeState::cached) {
			std::erase(trees.cached, node.box());
			run_begin = true;
		}
	}

	node.changeNodeState(parent.m_state);
	parent.m_children.emplace_back(node.box());
	node.m_parent = parent;

	computeDependencyGraph();

	if (run_begin) {
		node.propagateCallTick(node.info(), TickFunctionList::begin);
		node.enabled(true);
	}
	TOAST_TRACE("World", "Moved node {} ({}) under {} ({})", node.name(), node.uid(), parent.name(), parent.uid());

	return node.box();
}

auto World::dependencyGraphGraphviz() const -> std::string {
	auto escape = [](std::string_view value) {
		std::string result;
		result.reserve(value.size());
		for (char c : value) {
			if (c == '\\' || c == '"') {
				result.push_back('\\');
			}
			result.push_back(c);
		}
		return result;
	};

	auto stage_name = [](std::string_view value) {
		std::string result;
		result.reserve(value.size());
		for (char c : value) {
			result.push_back(c == ' ' ? '_' : c);
		}
		return result;
	};

	std::ostringstream out;
	out << "digraph DependencyGraph {\n";
	out << "  rankdir=LR;\n";
	out << "  node [shape=box];\n";

	auto emit_stage = [&](std::string_view stage_label, const auto& stage_schedule) {
		std::unordered_map<std::size_t, std::string> node_ids;
		std::unordered_map<std::size_t, std::string> node_labels;
		std::unordered_map<std::size_t, Box<Node>> scheduled_nodes;

		auto register_node = [&](const Box<Node>& node) {
			auto key = node.rid();
			auto [id_it, inserted] = node_ids.try_emplace(key, stage_name(stage_label) + "_n" + std::to_string(node_ids.size()));
			if (inserted) {
				node_labels.emplace(key, std::string(node->name()));
				scheduled_nodes.emplace(key, node);
			}
			return id_it->second;
		};

		for (const auto& wave : stage_schedule) {
			for (const auto& item : wave) {
				std::visit(
				    [&](const auto& value) {
					    using T = std::decay_t<decltype(value)>;
					    if constexpr (std::is_same_v<T, Box<Node>>) {
						    register_node(value);
					    } else {
						    for (const auto& node : value.nodes) {
							    register_node(node);
						    }
					    }
				    },
				    item
				);
			}
		}

		out << "  subgraph cluster_" << stage_name(stage_label) << " {\n";
		out << "    label=\"" << escape(stage_label) << "\";\n";
		out << "    color=\"lightgrey\";\n";

		for (const auto& [rid, label] : node_labels) {
			out << "    " << node_ids.at(rid) << " [label=\"" << escape(label) << "\"];\n";
		}

		for (std::size_t wave_index = 0; wave_index < stage_schedule.size(); ++wave_index) {
			const auto& wave = stage_schedule[wave_index];
			out << "    subgraph cluster_" << stage_name(stage_label) << "_wave_" << wave_index << " {\n";
			out << "      label=\"wave " << wave_index << "\";\n";
			out << "      rank=same;\n";

			std::size_t scc_index = 0;
			for (const auto& item : wave) {
				std::visit(
				    [&](const auto& value) {
					    using T = std::decay_t<decltype(value)>;
					    if constexpr (std::is_same_v<T, Box<Node>>) {
						    out << "      " << node_ids.at(value.rid()) << ";\n";
					    } else {
						    auto current_scc_index = scc_index++;
						    out << "      subgraph cluster_" << stage_name(stage_label) << "_wave_" << wave_index << "_scc_"
						        << current_scc_index << " {\n";
						    out << "        label=\"SCC " << current_scc_index << "\";\n";
						    out << "        color=\"gold\";\n";
						    for (const auto& node : value.nodes) {
							    out << "        " << node_ids.at(node.rid()) << ";\n";
						    }
						    out << "      }\n";
					    }
				    },
				    item
				);
			}
			out << "    }\n";
		}

		for (const auto& [rid, node] : scheduled_nodes) {
			std::queue<Box<Node>> q;
			std::unordered_set<std::size_t> visited;
			std::unordered_set<std::size_t> connected;

			if (auto it = dependency_graph.connections.find(node); it != dependency_graph.connections.end()) {
				for (const auto& succ : it->second) {
					q.push(succ);
					visited.insert(succ.rid());
				}
			}

			while (!q.empty()) {
				Box<Node> curr = q.front();
				q.pop();

				auto curr_rid = curr.rid();
				if (scheduled_nodes.contains(curr_rid)) {
					if (connected.insert(curr_rid).second) {
						out << "    " << node_ids.at(rid) << " -> " << node_ids.at(curr_rid) << ";\n";
					}
				} else {
					if (auto it = dependency_graph.connections.find(curr); it != dependency_graph.connections.end()) {
						for (const auto& succ : it->second) {
							if (visited.insert(succ.rid()).second) {
								q.push(succ);
							}
						}
					}
				}
			}
		}

		out << "  }\n";
	};

	emit_stage("early_tick", tick_schedule.early_tick);
	emit_stage("tick", tick_schedule.tick);
	emit_stage("post_physics", tick_schedule.post_physics);
	emit_stage("late_tick", tick_schedule.late_tick);

	out << "}\n";
	return out.str();
}

#pragma region TESTS

namespace _detail {

namespace {
auto testNodeInfos() -> std::unordered_map<const Node*, NodeInfo>& {
	static std::unordered_map<const Node*, NodeInfo> infos;
	return infos;
}
}

void WorldTestAccess::WorldDeleter::operator()(World* world) const noexcept {
	delete world;
}

auto WorldTestAccess::createWorld() -> WorldPtr {
	testNodeInfos().clear();
	return WorldPtr(new World());
}

auto WorldTestAccess::createNode(World& world, std::string_view name, NodeState state) -> Box<Node> {
	auto node = world.nodeAllocation();
	node->m_name = name;
	node->m_state = state;
	node->m_type = NodeType::child;
	node->m_local_enabled = true;
	node->m_inherited_enabled = true;

	NodeInfo& info = testNodeInfos()[&*node];
	info.type = "test::Node";
	info.functions.list = TickFunctionList::none;
	node->m_info = &info;

	return node;
}

void WorldTestAccess::registerDependency(Node& from, Node& to) {
	World::instance->registerDependency(from, to);
}

void WorldTestAccess::addTickStage(Node& node, TickFunctionList stage) {
	// Test-only: fabricate a per-instance NodeInfo carrying the requested tick flags so the
	// scheduler (which reads m_info) treats this node as participating in `stage`. Real nodes
	// get their NodeInfo from the type registry instead.
	NodeInfo& info = testNodeInfos()[&node];
	info.type = "test::Node";
	info.functions.list = info.functions.list | stage;
	node.m_info = &info;
}

auto WorldTestAccess::tickSchedule(World& world) noexcept -> TickSchedule& {
	return world.tick_schedule;
}

auto WorldTestAccess::dependencyGraph(World& world) noexcept -> World::DependencyGraph& {
	return world.dependency_graph;
}

void WorldTestAccess::computeDependencyGraph(World& world) {
	world.computeDependencyGraph();
}

auto WorldTestAccess::instantiate(
    World& world, const assets::AssetHandle<assets::Prefab>& file, INodeOwner::InstantiateContext& ctx
) -> Box<Node> {
	return world.instantiate(file, ctx);
}

auto WorldTestAccess::childrenOf(const Node& node) -> const std::vector<Box<Node>>& {
	return node.m_children;
}

auto WorldTestAccess::isPrefabInterior(const Node& node) -> bool {
	return node.m_prefab_interior;
}

void WorldTestAccess::initThreadPool() {
	static std::unique_ptr<ThreadPool> pool = ThreadPool::create();
	(void)pool;
}

void WorldTestAccess::setWorldRoot(World& world, Node& node) {
	world.trees.root = node.box();
}

void WorldTestAccess::initAssetManager(std::string_view assets_dir, std::string_view cache_dir) {
	static std::unique_ptr<assets::AssetManager> manager;

	std::filesystem::path assets_path {std::string(assets_dir)};
	std::filesystem::path cache_path {std::string(cache_dir)};
	assets::AssetManager::setPaths(
	    assets::Paths {
	      .assets = assets_path,
	      .artworks = assets_path,
	      .cache = cache_path,
	      .saved = assets_path,
	      .core = assets_path,
	    }
	);

	if (not manager) {
		manager = std::make_unique<assets::AssetManager>();    // ctor reads the manifest with the paths above
	} else {
		manager->reloadManifest();
	}
}

void WorldTestAccess::waitForLoads(World& world) {
	for (auto& future : world.m.load_futures) {
		if (future.valid()) {
			future.wait();
		}
	}
}

void WorldTestAccess::drainLoadQueue(World& world) {
	world.drainLoadQueue();
}

auto WorldTestAccess::spawnSync(
    World& world, const assets::AssetHandle<assets::Prefab>& file, Node& parent, INodeOwner::InstantiateContext& ctx
) -> Box<Node> {
	Box<Node> root = world.instantiate(file, ctx);
	if (not root.exists()) {
		return {};
	}
	World::generateUid(*root);
	root->changeNodeState(NodeState::cached);
	while (World::findNode(root->uid(), &parent).exists()) {
		World::generateUid(*root);
	}
	world.moveToChild(*root, parent);
	return root;
}

auto WorldTestAccess::dependencyGraphGraphviz(const World& world) -> std::string {
	return world.dependencyGraphGraphviz();
}

void WorldTestAccess::loadNode(toast::UID uid) {
	World::loadNode(uid);
}

auto WorldTestAccess::findCached(std::string_view name) -> Box<Node> {
	return World::findCached(name);
}

auto WorldTestAccess::findNode(const UID& uid, Node* scope) -> Box<Node> {
	return World::findNode(uid, scope);
}

auto WorldTestAccess::findNode(std::string_view path) -> Box<Node> {
	return World::findNode(path);
}

auto WorldTestAccess::uidPath(const Node& node) -> std::string {
	return World::uidPath(node);
}

}

#pragma endregion TESTS

}
