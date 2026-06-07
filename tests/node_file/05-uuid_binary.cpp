#include "toast/resources/node_file.hpp"
#include "toast/world/uuid.hpp"
#include "test_registry.hpp"

#include <cassert>
#include <span>
#include <sstream>
#include <vector>

using namespace toast;

// Regression test: uuid_t fields must survive a binary round-trip (the binary
// write_single/read_single and array paths previously had no uuid_t case).
TOAST_TEST_NAMED("node_file", "node_file/05-uuid_binary", test_node_file_05_uuid_binary) {
	const char* text =
	    "[node type=Foo]\n"
	    "my_ref @uuid = ABCDEFGHIJK\n"
	    "refs @array_uuid = LMNOPQRSTUV bcdefghijkl\n";

	std::stringstream ss(text);
	NodeFile nf(ss);

	assert(nf.nodes.size() == 1);
	assert(nf.nodes[0].fields.size() == 2);

	auto single = std::any_cast<UUID>(nf.nodes[0].fields[0].value);
	auto array = std::any_cast<std::vector<UUID>>(nf.nodes[0].fields[1].value);
	assert(single.data() != 0);    // parsed a real value, not a default UUID
	assert(array.size() == 2);

	// Binary round-trip (UUID exposes value via data(); it has no operator==)
	std::vector<uint8_t> binary = nf.toBinary();
	std::span<const uint8_t> bytes(binary);
	NodeFile from_binary(bytes);

	assert(from_binary.nodes.size() == 1);
	auto single_rt = std::any_cast<UUID>(from_binary.nodes[0].fields[0].value);
	auto array_rt = std::any_cast<std::vector<UUID>>(from_binary.nodes[0].fields[1].value);
	assert(single_rt.data() == single.data());
	assert(array_rt.size() == 2);
	assert(array_rt[0].data() == array[0].data());
	assert(array_rt[1].data() == array[1].data());

	// Text round-trip too, for good measure
	std::stringstream round(nf.toFile());
	NodeFile from_text(round);
	assert(std::any_cast<UUID>(from_text.nodes[0].fields[0].value).data() == single.data());
}
