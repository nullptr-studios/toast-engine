#include "../../engine/src/toast/assets/node_file.hpp"
#include "test_registry.hpp"
#include "sample.hpp"

#include <cassert>
#include <sstream>
#include <vector>
#include <fstream>

using namespace toast;

TOAST_TEST_NAMED("node_file", "node_file/03-to_binary", test_node_file_03_to_binary) {
	std::stringstream ss(sample_text);
	NodeFile nf(ss);
	
	std::vector<uint8_t> binary = nf.toBinary();
	assert(!binary.empty());
	
	// Check magic
	assert(binary.size() >= 6);
	assert(binary[0] == 'T' && binary[1] == 'N' && binary[2] == 'O' && binary[3] == 'D' && binary[4] == 'E');

	std::ofstream out("sample.tbnode", std::ios::binary);
	assert(out.is_open());
	out.write(reinterpret_cast<const char*>(binary.data()), binary.size());
	out.close();
}