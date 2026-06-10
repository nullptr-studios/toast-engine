#include "toast/resources/node_file.hpp"
#include <toast/uid.hpp>
#include "test_registry.hpp"

#include <cassert>
#include <span>
#include <sstream>
#include <vector>

using namespace toast;

// Regression test: uid_t fields must survive a binary round-trip (the binary
// write_single/read_single and array paths previously had no uid_t case).
TOAST_TEST_NAMED("node_file", "node_file/05-uid_binary", test_node_file_05_uid_binary) {
	const char* text =
	    "[node type=Foo]\n"
	    "my_ref @uid = ABCDEFGHIJK\n"
	    "refs @array_uid = LMNOPQRSTUV bcdefghijkl\n";

	std::stringstream ss(text);
	NodeFile nf(ss);

	assert(nf.nodes.size() == 1);
	assert(nf.nodes[0].fields.size() == 2);

	auto single = std::any_cast<UID>(nf.nodes[0].fields[0].value);
	auto array = std::any_cast<std::vector<UID>>(nf.nodes[0].fields[1].value);
	assert(single.data() != 0);    // parsed a real value, not a default UID
	assert(array.size() == 2);

	// Binary round-trip (UID exposes value via data(); it has no operator==)
	std::vector<uint8_t> binary = nf.toBinary();
	std::span<const uint8_t> bytes(binary);
	NodeFile from_binary(bytes);

	assert(from_binary.nodes.size() == 1);
	auto single_rt = std::any_cast<UID>(from_binary.nodes[0].fields[0].value);
	auto array_rt = std::any_cast<std::vector<UID>>(from_binary.nodes[0].fields[1].value);
	assert(single_rt.data() == single.data());
	assert(array_rt.size() == 2);
	assert(array_rt[0].data() == array[0].data());
	assert(array_rt[1].data() == array[1].data());

	// Text round-trip too, for good measure
	std::stringstream round(nf.toFile());
	NodeFile from_text(round);
	assert(std::any_cast<UID>(from_text.nodes[0].fields[0].value).data() == single.data());
}
