#include "dependency_graph_test_helpers.hpp"

#include "test_registry.hpp"

#include <string>
#include <vector>

using namespace toast::tests::dependency_graph;

TOAST_TEST_NAMED("Dependency Graph", "dependency_graph/08_graphviz", test_dependency_graph_08_graphviz) {
	toast::World world;

	auto make_node = [&](std::string_view name, std::initializer_list<Stage> stages) {
		auto node = toast::_detail::WorldTestAccess::createNode(world, name);
		for (auto stage : stages) {
			addStageFunction(*node, stage);
		}
		return node;
	};

	auto connect_cycle = [&](std::vector<toast::Box<toast::Node>>& nodes) {
		for (std::size_t i = 0; i < nodes.size(); ++i) {
			toast::World::registerDependency(*nodes[i], *nodes[(i + 1) % nodes.size()]);
		}
	};

	auto connect_chain = [&](std::vector<toast::Box<toast::Node>>& nodes) {
		for (std::size_t i = 0; i + 1 < nodes.size(); ++i) {
			toast::World::registerDependency(*nodes[i], *nodes[i + 1]);
		}
	};

	std::vector<toast::Box<toast::Node>> early_cluster {
	  make_node("e0", {Stage::early_tick}),
	  make_node("e1", {Stage::early_tick}),
	  make_node("e2", {Stage::early_tick}),
	};

	std::vector<toast::Box<toast::Node>> post_cluster {
	  make_node("p0", {Stage::post_physics}),
	  make_node("p1", {Stage::post_physics}),
	  make_node("p2", {Stage::post_physics}),
	  make_node("p3", {Stage::post_physics}),
	};

	std::vector<toast::Box<toast::Node>> late_cluster {
	  make_node("l0", {Stage::late_tick}),
	  make_node("l1", {Stage::late_tick}),
	  make_node("l2", {Stage::late_tick}),
	};

	std::vector<toast::Box<toast::Node>> tick_wave_0;
	tick_wave_0.reserve(20);
	for (int i = 0; i < 20; ++i) {
		tick_wave_0.push_back(make_node("tw0_" + std::to_string(i), {Stage::tick}));
	}

	std::vector<toast::Box<toast::Node>> tick_wave_1;
	tick_wave_1.reserve(15);
	for (int i = 0; i < 15; ++i) {
		tick_wave_1.push_back(make_node("tw1_" + std::to_string(i), {Stage::tick}));
	}

	std::vector<toast::Box<toast::Node>> tick_wave_2;
	tick_wave_2.reserve(10);
	for (int i = 0; i < 10; ++i) {
		tick_wave_2.push_back(make_node("tw2_" + std::to_string(i), {Stage::tick}));
	}

	std::vector<toast::Box<toast::Node>> tick_cluster_a {
	  make_node("t_c_a0", {Stage::tick}),
	  make_node("t_c_a1", {Stage::tick}),
	  make_node("t_c_a2", {Stage::tick}),
	  make_node("t_c_a3", {Stage::tick}),
	};

	std::vector<toast::Box<toast::Node>> tick_cluster_b {
	  make_node("t_c_b0", {Stage::tick}),
	  make_node("t_c_b1", {Stage::tick}),
	  make_node("t_c_b2", {Stage::tick}),
	};

	std::vector<toast::Box<toast::Node>> indep_graph_1;
	indep_graph_1.reserve(8);
	for(int i = 0; i < 8; ++i) {
		indep_graph_1.push_back(make_node("indep1_" + std::to_string(i), {Stage::tick}));
	}

	std::vector<toast::Box<toast::Node>> indep_graph_2;
	indep_graph_2.reserve(12);
	for(int i = 0; i < 12; ++i) {
		indep_graph_2.push_back(make_node("indep2_" + std::to_string(i), {Stage::tick}));
	}

	std::vector<toast::Box<toast::Node>> pruned_nodes;
	pruned_nodes.reserve(15);
	for (int i = 0; i < 15; ++i) {
		pruned_nodes.push_back(make_node("pruned_" + std::to_string(i), {}));
	}

	connect_cycle(early_cluster);
	connect_cycle(post_cluster);
	connect_cycle(late_cluster);
	connect_cycle(tick_cluster_a);
	connect_cycle(tick_cluster_b);

	// Main graph connections
	// Cross connections between waves to create complex layout
	for (int i = 0; i < 15; ++i) {
		toast::World::registerDependency(*tick_wave_0[i], *tick_wave_1[i]);
		if (i + 1 < 15) {
			toast::World::registerDependency(*tick_wave_0[i + 1], *tick_wave_1[i]);
		}
	}
	for (int i = 0; i < 10; ++i) {
		toast::World::registerDependency(*tick_wave_1[i], *tick_wave_2[i]);
		if (i + 2 < 15) {
			toast::World::registerDependency(*tick_wave_1[i + 2], *tick_wave_2[i]);
		}
	}

	// Tie clusters into the main graph waves
	toast::World::registerDependency(*tick_wave_0[5], *tick_cluster_a.front());
	toast::World::registerDependency(*tick_cluster_a.back(), *tick_wave_1[5]);

	toast::World::registerDependency(*tick_wave_1[8], *tick_cluster_b.front());
	toast::World::registerDependency(*tick_cluster_b.back(), *tick_wave_2[8]);

	// Independent Graph 1: Simple chain + fork
	connect_chain(indep_graph_1);
	toast::World::registerDependency(*indep_graph_1[2], *indep_graph_1[5]);

	// Independent Graph 2: Diamond pattern + small cluster
	toast::World::registerDependency(*indep_graph_2[0], *indep_graph_2[1]);
	toast::World::registerDependency(*indep_graph_2[0], *indep_graph_2[2]);
	toast::World::registerDependency(*indep_graph_2[1], *indep_graph_2[3]);
	toast::World::registerDependency(*indep_graph_2[2], *indep_graph_2[3]);
	for(int i = 4; i < 12; ++i) {
		toast::World::registerDependency(*indep_graph_2[3], *indep_graph_2[i]);
	}

	// Interleave pruned nodes across different parts of the graphs to test path tracing
	connect_chain(pruned_nodes);
	toast::World::registerDependency(*tick_wave_0[18], *pruned_nodes[0]);
	toast::World::registerDependency(*pruned_nodes[3], *tick_wave_1[14]);

	toast::World::registerDependency(*indep_graph_1[0], *pruned_nodes[5]);
	toast::World::registerDependency(*pruned_nodes[7], *indep_graph_1[7]);

	toast::World::registerDependency(*pruned_nodes[14], *post_cluster.front());
	toast::World::registerDependency(*late_cluster.back(), *pruned_nodes[12]);

	world.computeDependencyGraph();
	const auto dot = world.dependencyGraphGraphviz();
	TOAST_TRACE("Tests", "\n{}", dot);

	assert(dot.find("cluster_early_tick") != std::string::npos);
	assert(dot.find("cluster_tick") != std::string::npos);
	assert(dot.find("cluster_post_physics") != std::string::npos);
	assert(dot.find("cluster_late_tick") != std::string::npos);
	assert(dot.find("wave 0") != std::string::npos);
	assert(dot.find("wave 1") != std::string::npos);
	assert(dot.find("wave 2") != std::string::npos);
	assert(dot.find("SCC 0") != std::string::npos);
	assert(dot.find("tw0_19") != std::string::npos);
	assert(dot.find("tw1_14") != std::string::npos);
	assert(dot.find("tw2_9") != std::string::npos);
	assert(dot.find("indep1_7") != std::string::npos);
	assert(dot.find("indep2_11") != std::string::npos);
}
