#include "../../engine/src/toast/assets/node_file.hpp"
#include "test_registry.hpp"
#include "sample.hpp"

#include <cassert>
#include <sstream>
#include <vector>
#include <fstream>
#include <iostream>

using namespace toast;

TOAST_TEST_NAMED("node_file", "node_file/04-from_binary", test_node_file_04_from_binary) {
	std::ifstream in("sample.tbnode", std::ios::binary | std::ios::ate);
	assert(in.is_open());
	std::streamsize size = in.tellg();
	in.seekg(0, std::ios::beg);
	
	std::vector<uint8_t> binary(size);
	if (in.read(reinterpret_cast<char*>(binary.data()), size)) {
		NodeFile nf(binary);
		std::string output = nf.toFile();
		
		if (output != sample_text) {
			std::cerr << "Binary read output mismatch!\nExpected:\n" << sample_text << "\nActual:\n" << output << std::endl;
		}
		assert(output == sample_text);
	} else {
		assert(false && "Failed to read binary file");
	}
}