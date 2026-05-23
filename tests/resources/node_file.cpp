#include "toast/resources/node_file.hpp"
#include "test_registry.hpp"

#include <cassert>
#include <sstream>
#include <vector>
#include <glm/glm.hpp>

using namespace toast;

namespace {
	const std::string sample_text = 
		"global_int @int = 42\n"
		"\n"
		"[Player type=Entity]\n"
		"hp @float = 100\n"
		"pos @vec3 = 1 2 3\n"
		"tags @array_string = tag1\x1ftag2\n"
		".Transform\n"
		"    scale @float = 1\n"
		"    ..Physics\n"
		"        velocity @vec3 = 0 0 0\n";
}

TOAST_TEST_NAMED("resources", "resources/node_file/01-from_file", test_node_file_from_file) {
	std::stringstream ss(sample_text);
	NodeFile nf(ss);

	assert(nf.global_items.size() == 1);
	assert(nf.global_items[0].name == "global_int");
	assert(std::any_cast<int>(nf.global_items[0].value) == 42);

	assert(nf.nodes.size() == 1);
	assert(nf.nodes[0].name == "Player");
	assert(nf.nodes[0].type == "Entity");
	assert(nf.nodes[0].items.size() == 3);
	assert(nf.nodes[0].groups.size() == 1);
	assert(nf.nodes[0].groups[0].name == "Transform");
	assert(nf.nodes[0].groups[0].subgroups.size() == 1);
	assert(nf.nodes[0].groups[0].subgroups[0].name == "Physics");
}

TOAST_TEST_NAMED("resources", "resources/node_file/02-to_file", test_node_file_to_file) {
	std::stringstream ss(sample_text);
	NodeFile nf(ss);
	
	std::string output = nf.toFile();
	
	// Re-parse the output to verify consistency
	std::stringstream ss2(output);
	NodeFile nf2(ss2);

	assert(nf2.global_items.size() == nf.global_items.size());
	assert(nf2.nodes.size() == nf.nodes.size());
	assert(nf2.nodes[0].name == nf.nodes[0].name);
	assert(nf2.nodes[0].groups[0].subgroups[0].items.size() == nf.nodes[0].groups[0].subgroups[0].items.size());
}

TOAST_TEST_NAMED("resources", "resources/node_file/03-to_binary", test_node_file_to_binary) {
	std::stringstream ss(sample_text);
	NodeFile nf(ss);
	
	std::vector<uint8_t> binary = nf.toBinary();
	assert(!binary.empty());
	
	// Check magic
	assert(binary.size() >= 6);
	assert(binary[0] == 'T' && binary[1] == 'N' && binary[2] == 'O' && binary[3] == 'D' && binary[4] == 'E');
}

TOAST_TEST_NAMED("resources", "resources/node_file/04-from_binary", test_node_file_from_binary) {
	std::stringstream ss(sample_text);
	NodeFile nf(ss);
	
	std::vector<uint8_t> binary = nf.toBinary();
	NodeFile nf2(binary);

	assert(nf2.global_items.size() == nf.global_items.size());
	assert(nf2.global_items[0].name == nf.global_items[0].name);
	assert(std::any_cast<int>(nf2.global_items[0].value) == std::any_cast<int>(nf.global_items[0].value));

	assert(nf2.nodes.size() == nf.nodes.size());
	assert(nf2.nodes[0].name == nf.nodes[0].name);
	assert(nf2.nodes[0].items.size() == nf.nodes[0].items.size());
	
	// Check deep nested item
	auto& v1 = std::any_cast<glm::vec3>(nf.nodes[0].groups[0].subgroups[0].items[0].value);
	auto& v2 = std::any_cast<glm::vec3>(nf2.nodes[0].groups[0].subgroups[0].items[0].value);
	assert(v1 == v2);
}
