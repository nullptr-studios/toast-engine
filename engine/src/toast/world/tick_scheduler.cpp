#include "tick_scheduler.hpp"

#include "tree_lock.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <stack>
#include <thread>
#include <toast/log.hpp>
#include <toast/scripting/lua_state.hpp>
#include <toast/scripting/script_dispatch.hpp>
#include <toast/scripting/script_runtime.hpp>
#include <toast/thread_pool.hpp>
#include <unordered_set>

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

void NodeCluster::physicsTick() {
	for (auto& node : nodes) {
		node->callTick(node->info(), TickFunctionList::physics_tick);
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
		if (node->hasTickFunction(TickFunctionList::early_tick)) {
			return true;
		}
	}
	return false;
}

auto NodeCluster::hasTick() -> bool {
	for (auto& node : nodes) {
		if (node->hasTickFunction(TickFunctionList::tick)) {
			return true;
		}
	}
	return false;
}

auto NodeCluster::hasPhysicsTick() -> bool {
	for (auto& node : nodes) {
		if (node->hasTickFunction(TickFunctionList::physics_tick)) {
			return true;
		}
	}
	return false;
}

auto NodeCluster::hasPostPhysics() -> bool {
	for (auto& node : nodes) {
		if (node->hasTickFunction(TickFunctionList::post_physics)) {
			return true;
		}
	}
	return false;
}

auto NodeCluster::hasLateTick() -> bool {
	for (auto& node : nodes) {
		if (node->hasTickFunction(TickFunctionList::late_tick)) {
			return true;
		}
	}
	return false;
}
}

#pragma endregion NODE_CLUSTER

namespace _detail {

namespace {

/// Adds the interpreters an item runs Lua on while it executes `func`: the ones its own scripts use, and the ones of the
/// nodes it declared an interaction with, where the calls it makes land
void interpretersOf(
    const std::variant<Box<Node>, NodeCluster>& item, TickFunctionList func, const Interactions* interactions,
    std::vector<size_t>& out
) {
	auto add = [&](const Node& node) {
		if (const auto interpreter = node.scriptVm(func)) {
			out.push_back(*interpreter);
		}
		if (interactions == nullptr || interactions->empty()) {
			return;
		}
		const auto partners = interactions->find(node.box());
		if (partners == interactions->end()) {
			return;
		}
		for (const Box<Node>& partner : partners->second) {
			if (!partner.exists()) {
				continue;
			}
			if (const auto interpreter = partner->scriptVmAny()) {
				out.push_back(*interpreter);
			}
		}
	};

	if (const auto* node = std::get_if<Box<Node>>(&item)) {
		add(**node);
		return;
	}
	for (const auto& node : std::get<NodeCluster>(item).nodes) {
		add(*node);
	}
}

/// Union find over interpreter numbers, to tie together the ones a cluster runs on
class InterpreterSets {
public:
	auto find(size_t interpreter) -> size_t {
		size_t root = interpreter;
		while (true) {
			const auto [it, inserted] = m_parent.try_emplace(root, root);
			if (it->second == root) {
				break;
			}
			root = it->second;
		}
		// Point everything on the way at the root
		while (interpreter != root) {
			const size_t next = m_parent[interpreter];
			m_parent[interpreter] = root;
			interpreter = next;
		}
		return root;
	}

	void unite(size_t first, size_t second) { m_parent[find(first)] = find(second); }

private:
	std::unordered_map<size_t, size_t> m_parent;
};

/// True when every item of the wave still runs its Lua on the interpreters of the job the plan put it in
auto planMatches(
    const TickSchedule::Wave& wave, TickFunctionList func, const Interactions* interactions, const std::vector<WaveJob>& jobs
) -> bool {
	size_t items = 0;
	std::vector<size_t> found;
	for (const WaveJob& job : jobs) {
		for (const size_t item : job.items) {
			if (item >= wave.size()) {
				return false;
			}
			found.clear();
			interpretersOf(wave[item], func, interactions, found);
			if (found.empty() != job.interpreters.empty()) {
				return false;
			}
			for (const size_t interpreter : found) {
				if (std::ranges::find(job.interpreters, interpreter) == job.interpreters.end()) {
					return false;
				}
			}
		}
		items += job.items.size();
	}
	return items == wave.size();
}
}

auto planWave(const TickSchedule::Wave& wave, TickFunctionList func, const Interactions* interactions) -> std::vector<WaveJob> {
	std::vector<std::vector<size_t>> interpreters(wave.size());
	InterpreterSets sets;
	for (size_t i = 0; i < wave.size(); ++i) {
		interpretersOf(wave[i], func, interactions, interpreters[i]);
		for (size_t k = 1; k < interpreters[i].size(); ++k) {
			sets.unite(interpreters[i][0], interpreters[i][k]);
		}
	}

	std::vector<WaveJob> scripted;
	std::vector<WaveJob> plain;
	std::unordered_map<size_t, size_t> job_of_set;    // root interpreter of a set -> its job in `scripted`
	for (size_t i = 0; i < wave.size(); ++i) {
		if (interpreters[i].empty()) {
			plain.push_back(WaveJob {.items = {i}, .interpreters = {}});
			continue;
		}
		const auto [it, inserted] = job_of_set.try_emplace(sets.find(interpreters[i][0]), scripted.size());
		if (inserted) {
			scripted.emplace_back();
		}
		WaveJob& job = scripted[it->second];
		job.items.push_back(i);
		for (const size_t interpreter : interpreters[i]) {
			if (std::ranges::find(job.interpreters, interpreter) == job.interpreters.end()) {
				job.interpreters.push_back(interpreter);
			}
		}
	}

	// The biggest job first: it is the one that decides how long the wave takes
	std::ranges::stable_sort(scripted, [](const WaveJob& lhs, const WaveJob& rhs) { return lhs.items.size() > rhs.items.size(); });

	std::vector<WaveJob> jobs = std::move(scripted);
	jobs.insert(jobs.end(), std::make_move_iterator(plain.begin()), std::make_move_iterator(plain.end()));
	return jobs;
}
}

auto TickScheduler::registerDependency(Node& from, Node& to) -> bool {
	if (&from == &to) {
		TOAST_WARN("World", "{} ({}) tried to register a dependency to itself", from.name(), from.uid());
		return false;
	}

	std::scoped_lock lock(graph_mutex);

	// don't store duplicates
	auto& edges = graph.connections[from];
	if (std::ranges::contains(edges, Box<Node>(to))) {
		return false;
	}

	edges.emplace_back(to);
	graph.inverse_connections[to].emplace_back(from);
	TOAST_TRACE("World", "Added dependency from {} to {}", from.name(), from.uid());
	return true;
}

auto TickScheduler::registerInteraction(Node& first, Node& second) -> bool {
	if (&first == &second) {
		return false;
	}

	std::scoped_lock lock(graph_mutex);
	const Box<Node> first_box(first);
	const Box<Node> second_box(second);
	auto& partners = interactions[first_box];
	if (std::ranges::contains(partners, second_box)) {
		return false;
	}
	partners.emplace_back(second_box);
	interactions[second_box].emplace_back(first_box);
	TOAST_TRACE("World", "{} ({}) and {} ({}) interact", first.name(), first.uid(), second.name(), second.uid());
	return true;
}

auto TickScheduler::unregisterDependency(Node& from, Node& to) -> bool {
	std::scoped_lock lock(graph_mutex);
	const Box<Node> from_box(from);
	const Box<Node> to_box(to);
	const auto connections = graph.connections.find(from_box);
	if (connections == graph.connections.end() || std::erase(connections->second, to_box) == 0) {
		return false;
	}

	// Remove the dependency from the inverse graph
	if (const auto inverse_connections = graph.inverse_connections.find(to_box);
	    inverse_connections != graph.inverse_connections.end()) {
		std::erase(inverse_connections->second, from_box);
	}
	return true;
}

void TickScheduler::compute(const std::vector<Box<Node>>& all_nodes) {
	ZoneScoped;
	TOAST_TRACE("World", "Computing dependency graph");
	std::scoped_lock lock(graph_mutex);

	// Guarantee every existing node exists in the subgraph
	for (const auto& node : all_nodes) {
		graph.connections[node];
		graph.inverse_connections[node];
	}

	auto subgraphs = subgraphSeparation(all_nodes);

	// We are gonna remove nodes that will not be able to get ticked because:
	//		1) they don't have any tick functions
	//		2) they are not in an active state
	for (auto& g : subgraphs) {
		std::erase_if(g, [](const auto& n) { return not n->hasTickFunction(TickFunctionList::tick_mask); });

		std::erase_if(g, [](const auto& n) { return n->m_state != NodeState::root && n->m_state != NodeState::global; });
	}

	// Drop subgraphs that became entirely empty
	std::erase_if(subgraphs, [](const auto& g) { return g.empty(); });

	auto result = tarjanAlgorithm(subgraphs);

	auto waves = assignWaves(result);
	auto ts = optimizeWaves(waves);
	schedule = std::move(ts);
	m_plans.clear();
	TOAST_TRACE(
	    "World",
	    "Dependency graph: early={} tick={} physics={} post_physics={} late={} waves",
	    schedule.early_tick.size(),
	    schedule.tick.size(),
	    schedule.physics_tick.size(),
	    schedule.post_physics.size(),
	    schedule.late_tick.size()
	);
}

void TickScheduler::tickItem(const TickSchedule::Wave::value_type& item, TickFunctionList func) {
	// A Box only hands out a mutable node to a handle that is not const
	if (const auto* held = std::get_if<Box<Node>>(&item)) {
		Box<Node> node = *held;
		node->callTick(node->info(), func);
		return;
	}

	// Clusters tick their nodes synchronously
	for (auto node : std::get<NodeCluster>(item).nodes) {
		node->callTick(node->info(), func);
	}
}

auto TickScheduler::planFor(const TickSchedule::Wave& wave, TickFunctionList func) const -> const std::vector<WaveJob>& {
	// Scripts declare interactions from init()
	std::scoped_lock lock(graph_mutex);
	CachedPlan& plan = m_plans[&wave];
	if (plan.func != func || !planMatches(wave, func, &interactions, plan.jobs)) {
		plan.jobs = planWave(wave, func, &interactions);
		plan.func = func;
	}
	return plan.jobs;
}

void TickScheduler::runWave(const TickSchedule::Wave& wave, const std::vector<WaveJob>& jobs, TickFunctionList func, int number) {
	ZoneScopedN("Wave");    // NOLINT
	ZoneNameF("Wave #%i: %zu items in %zu jobs", number, wave.size(), jobs.size());

	auto tick_item = [&wave, func](size_t item) { tickItem(wave[item], func); };
	runJobs(jobs, wave.size(), tick_item);
}

void TickScheduler::runPhase(const std::vector<TickSchedule::Wave>& phase, TickFunctionList func, std::string_view name) const {
	ZoneScoped;    // NOLINT
	ZoneNameF("%s", name.data());

	int number = 0;
	for (const auto& wave : phase) {
		runWave(wave, planFor(wave, func), func, ++number);

		scripting::ScriptDispatch::deliver();
	}
}

namespace {

constexpr size_t k_min_scripts_for_parallel_lifecycle = 16;

// tf is this
auto serialLifecycleRequested() -> bool {
	static const bool requested = [] {
#ifdef _WIN32
		char* value = nullptr;
		size_t size = 0;
		const bool found = _dupenv_s(&value, &size, "TOAST_SERIAL_LIFECYCLE") == 0 && value != nullptr;
		const bool on = found && value[0] != '\0' && value[0] != '0';
		std::free(value);    // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
		return on;
#else
		const char* value = std::getenv("TOAST_SERIAL_LIFECYCLE");
		return value != nullptr && value[0] != '\0' && value[0] != '0';
#endif
	}();
	return requested;
}
}

void TickScheduler::runLifecycle(Node& root, TickFunctionList stage) const {
	ZoneScoped;
	ZoneNameF("lifecycle(%s) %s", tickFunctionName(stage).data(), root.name().data());

	const bool enabling = stage == TickFunctionList::on_enable;
	auto one_node_at_a_time = [&] {
		if (enabling) {
			root.propagateEnable();
		} else {
			root.propagateCallTick(root.info(), stage);
		}
	};

	// A thread that is a worker cannot wait for the pool
	if (!scripting::LuaState::exists() || ThreadPool::workerCount() == 0 || ThreadPool::onWorkerThread() ||
	    scripting::LuaState::ownsAnyState() || serialLifecycleRequested()) {
		one_node_at_a_time();
		return;
	}

	size_t scripted = 0;
	std::vector<size_t> interpreters;
	{
		const TreeReadLock lock(root);
		auto count = [&](this auto&& self, const Node& node) -> void {
			if (node.m_script_runtime && node.m_script_runtime->hasTick(stage)) {
				++scripted;
				if (const size_t interpreter = node.m_script_runtime->stateIndex();
				    std::ranges::find(interpreters, interpreter) == interpreters.end()) {
					interpreters.push_back(interpreter);
				}
			}
			for (const Box<Node>& child : node.m_children) {
				if (child.exists()) {
					self(*child);
				}
			}
		};
		count(root);
	}
	if (scripted < k_min_scripts_for_parallel_lifecycle || interpreters.size() < 2) {
		one_node_at_a_time();
		return;
	}

	// The C++ functions go first, then the lua scripts
	auto run_level = [&](std::vector<Box<Node>>& level) {
		{
			ZoneNamedN(native_zone, "C++ functions", true);    // NOLINT
			for (Box<Node>& node : level) {
				if (node.exists()) {
					node->callTickNative(node->info(), stage);
				}
			}
		}

		TickSchedule::Wave wave;
		for (Box<Node>& node : level) {
			if (node.exists() && node->m_script_runtime && node->m_script_runtime->hasTick(stage)) {
				wave.emplace_back(node);
			}
		}
		if (wave.empty()) {
			return;
		}

		std::vector<WaveJob> jobs;
		{
			std::scoped_lock lock(graph_mutex);
			jobs = planWave(wave, stage, &interactions);
		}
		auto run_scripts = [&wave, stage](size_t item) {
			Box<Node> node = std::get<Box<Node>>(wave[item]);
			if (!node.exists()) {      // an earlier script of the level destroyed it
				return;
			}
			ZoneScopedN("Scripts");    // NOLINT
			ZoneNameF("%s callScripts(%s)", node->name().data(), tickFunctionName(stage).data());
			node->callTickScripts(stage);
		};
		if (jobs.size() > 1) {
			runJobs(jobs, wave.size(), run_scripts, {.non_blocking = false, .label = "Lifecycle worker"});
		} else {
			for (size_t item = 0; item < wave.size(); ++item) {
				run_scripts(item);
			}
		}

		// Calls between scripts that found their interpreter busy while the level ran
		scripting::ScriptDispatch::deliver();
	};

	std::vector<Box<Node>> level;
	if (!enabling || root.enabled()) {
		level.push_back(root.box());
	}
	for (int depth = 0; !level.empty(); ++depth) {
		ZoneScopedN("Level");    // NOLINT
		ZoneNameF("Level %i: %zu nodes", depth, level.size());

		std::vector<std::vector<Box<Node>>> below;
		below.reserve(level.size());
		for (Box<Node>& node : level) {
			below.push_back(node.exists() ? node->childrenSnapshot() : std::vector<Box<Node>> {});
		}

		run_level(level);

		std::vector<Box<Node>> next;
		for (auto& children : below) {
			for (Box<Node>& child : children) {
				if (!child.exists()) {
					continue;
				}
				if (enabling) {
					child->m_inherited_enabled = true;
					if (!child->enabled()) {
						continue;
					}
				}
				next.push_back(std::move(child));
			}
		}
		level = std::move(next);
	}
}

void TickScheduler::runPhaseSerial(
    const std::vector<TickSchedule::Wave>& phase, TickFunctionList func, std::string_view name
) const {
	ZoneScoped;    // NOLINT
	ZoneNameF("%s", name.data());

	for (const auto& wave : phase) {
		for (const auto& item : wave) {
			if (std::holds_alternative<Box<Node>>(item)) {
				auto node = std::get<Box<Node>>(item);
				node->callTick(node->info(), func);
				continue;
			}
			for (auto node : std::get<NodeCluster>(item).nodes) {
				node->callTick(node->info(), func);
			}
		}
	}
}

void TickScheduler::run() const {
	ZoneScoped;

	runPhase(schedule.early_tick, TickFunctionList::early_tick, "early_tick");
	runPhase(schedule.tick, TickFunctionList::tick, "tick");
	// TODO: physics step goes between tick and post_physics
	runPhase(schedule.post_physics, TickFunctionList::post_physics, "post_physics");
	runPhase(schedule.late_tick, TickFunctionList::late_tick, "late_tick");
}

auto TickScheduler::subgraphSeparation(const std::vector<Box<Node>>& all_nodes) -> std::vector<std::vector<Box<Node>>> {
	ZoneScoped;

	using NodeGraph = std::vector<Box<Node>>;
	std::unordered_set<Box<Node>> visited;
	std::vector<NodeGraph> subgraphs;

	for (const auto& start_node : all_nodes) {
		if (visited.contains(start_node)) {
			continue;
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
			if (auto it = graph.connections.find(current); it != graph.connections.end()) {
				for (const auto& neighbor : it->second) {
					if (visited.contains(neighbor)) {
						continue;
					}
					visited.insert(neighbor);
					queue.push(neighbor);
				}
			}

			// Walk backward edges
			if (auto it = graph.inverse_connections.find(current); it != graph.inverse_connections.end()) {
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
	}

	return subgraphs;
}

/**
 * produces SCCs in reverse topological order so assignWaves iterates in the right direction;
 * SCCs with more than one node indicate a dependency cycle and are bundled into a NodeCluster
 */
auto TickScheduler::tarjanAlgorithm(const std::vector<std::vector<Box<Node>>>& input_subgraphs)
    -> std::vector<TickSchedule::Wave> {
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
			if (auto it = graph.connections.find(current_node); it != graph.connections.end()) {
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
auto TickScheduler::assignWaves(const std::vector<TickSchedule::Wave>& subgraphs) -> std::vector<TickSchedule::Wave> {
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
				auto it = graph.inverse_connections.find(node);
				if (it == graph.inverse_connections.end()) {
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
auto TickScheduler::optimizeWaves(const std::vector<TickSchedule::Wave>& waves) -> TickSchedule {
	TickSchedule schedule = {
	  .early_tick = waves,
	  .tick = waves,
	  .physics_tick = waves,
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

	filter_and_assign_wave(schedule.early_tick, 0, [](auto n) { return n->hasTickFunction(TickFunctionList::early_tick); });
	filter_and_assign_wave(schedule.tick, 1, [](auto n) { return n->hasTickFunction(TickFunctionList::tick); });
	filter_and_assign_wave(schedule.post_physics, 2, [](auto n) { return n->hasTickFunction(TickFunctionList::post_physics); });
	filter_and_assign_wave(schedule.late_tick, 3, [](auto n) { return n->hasTickFunction(TickFunctionList::late_tick); });
	filter_and_assign_wave(schedule.physics_tick, 4, [](auto n) { return n->hasTickFunction(TickFunctionList::physics_tick); });

	return schedule;
}

}
