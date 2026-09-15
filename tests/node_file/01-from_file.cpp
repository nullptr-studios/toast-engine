#include "../../engine/src/toast/assets/prefab.hpp"
#include "test_registry.hpp"
#include "sample.hpp"

#include <cassert>
#include <sstream>

using namespace toast;
using namespace assets;

TOAST_TEST_NAMED("node_file", "node_file/01-from_file", test_node_file_01_from_file) {
	std::stringstream ss(sample_text);
	Prefab nf(ss);

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

	auto parsed_strings = Prefab::valueFromString(
	    FieldType::string_t, true, "\"contains spaces\" \"\" \"a \\\"quote\\\" and a \\\\ slash\"");
	assert(parsed_strings.has_value());
	const auto& strings = std::any_cast<const std::vector<std::string>&>(*parsed_strings);
	assert((strings == std::vector<std::string> {"contains spaces", "", "a \"quote\" and a \\ slash"}));
	assert(Prefab::stringifyValue(FieldType::string_t, true, *parsed_strings)
	       == "\"contains spaces\" \"\" \"a \\\"quote\\\" and a \\\\ slash\"");

	std::stringstream multiline {
	    "[root type=toast::Node]\n"
	    "values @array_string = \"first value\" \\\n"
	    "    \"second value\" \\\n"
	    "\t\"third value\"\n"
	};
	Prefab multiline_prefab(multiline);
	const auto& values = std::any_cast<const std::vector<std::string>&>(multiline_prefab.nodes[0].fields[0].value);
	assert((values == std::vector<std::string> {"first value", "second value", "third value"}));

	Prefab wrapped_prefab;
	wrapped_prefab.nodes.push_back({
	    .name = "root",
	    .type = "toast::Node",
	    .fields = {{
	        .name = "values",
	        .type = FieldType::string_t,
	        .is_array = true,
	        .value = std::vector<std::string> {"first value", "second value", "third value", "fourth value", "fifth value"}
	    }}
	});
	const std::string wrapped = wrapped_prefab.toFile();
	const std::string wrapped_indent = "    ";
	assert(wrapped.contains("values @array_string = \\\n"));
	assert(wrapped.contains(wrapped_indent + "\"first value\" \\\n"));
	assert(wrapped.contains(wrapped_indent + "\"second value\" \\\n"));
	assert(wrapped.contains(wrapped_indent + "\"third value\" \\\n"));
	assert(wrapped.contains(wrapped_indent + "\"fourth value\" \\\n"));
	assert(wrapped.contains(wrapped_indent + "\"fifth value\"\n"));
	std::stringstream wrapped_stream(wrapped);
	Prefab reparsed_wrapped(wrapped_stream);
	const auto& reparsed_values =
	    std::any_cast<const std::vector<std::string>&>(reparsed_wrapped.nodes[0].fields[0].value);
	assert((reparsed_values == std::vector<std::string> {"first value", "second value", "third value", "fourth value", "fifth value"}));

	std::stringstream signal_text {
	    "[root type=toast::Node]\n"
	    "activated @signal = ABCDEFGHIJK \"on_activated\" \\\n"
	    "    LMNOPQRSTUV \"on_other_activated\"\n"
	};
	Prefab signal_prefab(signal_text);
	assert(signal_prefab.nodes[0].signals.size() == 1);
	const auto& signal = signal_prefab.nodes[0].signals[0];
	assert(signal.name == "activated");
	assert(signal.connections.size() == 2);
	assert(signal.connections[0].target.data() != 0 && signal.connections[0].function == "on_activated");
	assert(signal.connections[1].target.data() != 0 && signal.connections[1].function == "on_other_activated");

	Prefab wrapped_signals;
	Prefab::Signal long_signal {.name = "activated"};
	for (uint64_t i = 1; i <= 5; ++i) {
		long_signal.connections.push_back({.target = UID(i), .function = "on_a_very_long_signal_function"});
	}
	wrapped_signals.nodes.push_back({.name = "root", .type = "toast::Node", .signals = {std::move(long_signal)}});
	const std::string signal_output = wrapped_signals.toFile();
	assert(signal_output.contains("activated @signal = \\\n"));
	assert(signal_output.contains("    "));
	assert(signal_output.contains("\"on_a_very_long_signal_function\" \\\n"));
	const std::vector<uint8_t> signal_binary = wrapped_signals.toBinary();
	Prefab binary_signals{std::span<const uint8_t>(signal_binary)};
	assert(binary_signals.nodes.size() == 1);
	assert(binary_signals.nodes[0].signals.size() == 1);
	assert(binary_signals.nodes[0].signals[0].name == "activated");
	assert(binary_signals.nodes[0].signals[0].connections.size() == 5);
	assert(binary_signals.nodes[0].signals[0].connections[0].target.data() == 1);
	assert(binary_signals.nodes[0].signals[0].connections[0].function == "on_a_very_long_signal_function");
}
