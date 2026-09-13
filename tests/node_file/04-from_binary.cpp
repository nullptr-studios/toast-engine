#include "../../engine/src/toast/assets/prefab.hpp"
#include "test_registry.hpp"
#include "sample.hpp"

#include <cassert>
#include <sstream>
#include <vector>
#include <iostream>

using namespace toast;
using namespace assets;

TOAST_TEST_NAMED("node_file", "node_file/04-from_binary", test_node_file_04_from_binary) {
	// Bake the sample in memory so this test doesn't depend on a file left behind by another test
	std::stringstream ss(sample_text);
	const std::vector<uint8_t> binary = Prefab(ss).toBinary();
	assert(!binary.empty());

	Prefab nf(binary);
	std::string output = nf.toFile();

	if (output != sample_text) {
		std::cerr << "Binary read output mismatch!\nExpected:\n" << sample_text << "\nActual:\n" << output << std::endl;
	}
	assert(output == sample_text);
}