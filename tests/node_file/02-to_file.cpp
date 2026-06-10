#include "../../engine/src/toast/assets/node_file.hpp"
#include "test_registry.hpp"
#include "sample.hpp"

#include <cassert>
#include <sstream>
#include <iostream>

using namespace toast;

TOAST_TEST_NAMED("node_file", "node_file/02-to_file", test_node_file_02_to_file) {
	std::stringstream ss(sample_text);
	NodeFile nf(ss);
	
	std::string output = nf.toFile();
	
	if (output != sample_text) {
		std::cerr << "Output mismatch!\nExpected:\n" << sample_text << "\nActual:\n" << output << std::endl;
	}
	assert(output == sample_text);
}