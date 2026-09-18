#include "../../engine/src/toast/assets/prefab.hpp"
#include "test_registry.hpp"
#include "sample.hpp"

#include <cassert>
#include <sstream>
#include <vector>

using namespace toast;
using namespace assets;

TOAST_TEST_NAMED("node_file", "node_file/03-to_binary", test_node_file_03_to_binary) {
	std::stringstream ss(sample_text);
	Prefab nf(ss);
	
	std::vector<uint8_t> binary = nf.toBinary();
	assert(!binary.empty());
	
	// Check magic
	assert(binary.size() >= 6);
	assert(binary[0] == 'T' && binary[1] == 'N' && binary[2] == 'O' && binary[3] == 'D' && binary[4] == 'E');
}