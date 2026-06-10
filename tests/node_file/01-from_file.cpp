#include "../../engine/src/toast/assets/node_file.hpp"
#include "test_registry.hpp"
#include "sample.hpp"

#include <cassert>
#include <sstream>

using namespace toast;

TOAST_TEST_NAMED("node_file", "node_file/01-from_file", test_node_file_01_from_file) {
	std::stringstream ss(sample_text);
	NodeFile nf(ss);

	assert(nf.nodes.size() == 6);
	assert(nf.nodes[0].name == "station_master");
	assert(nf.nodes[0].type == "SpaceStationManager");
	
	// Check deep nested data
	assert(nf.nodes[4].name == "interactive_synth_console");
	assert(nf.nodes[4].groups.size() == 2);
	assert(nf.nodes[4].groups[1].name == "audio_engine");
	assert(nf.nodes[4].groups[1].subgroups.size() == 1);
	assert(nf.nodes[4].groups[1].subgroups[0].name == "synth_parameters");
	assert(nf.nodes[4].groups[1].subgroups[0].fields.size() == 4);

	auto& DetuneCents = nf.nodes[4].groups[1].subgroups[0].fields[1];
	assert(DetuneCents.name == "detune_cents");
	assert(std::any_cast<float>(DetuneCents.value) == 14.25f);
}