#include "test_registry.hpp"

#include <cassert>
#include <cstring>
#include <string>
#include <toast/renderer/spirv_entry_points.hpp>
#include <vector>

namespace {

using renderer::spirv::ExecutionModel;

/// @brief Assembles a minimal SPIR-V module declaring one OpEntryPoint per entry
///
/// Hand-built rather than compiled, so the test covers the parser rather than whatever Slang happens to emit -
/// including the padding and bounds cases a real module never produces
auto makeModule(const std::vector<std::pair<ExecutionModel, std::string>>& entries) -> std::vector<std::byte> {
	std::vector<uint32_t> words {0x07230203, 0x00010600, 0, 1, 0};

	for (const auto& [model, name] : entries) {
		// Null-terminated and zero-padded up to a word boundary, which is what the spec requires
		const size_t name_words = (name.size() / 4) + 1;
		std::vector<uint32_t> packed(name_words, 0);
		std::memcpy(packed.data(), name.data(), name.size());

		const uint32_t instruction_words = 3 + static_cast<uint32_t>(name_words);
		words.push_back((instruction_words << 16) | 15u);
		words.push_back(static_cast<uint32_t>(model));
		words.push_back(42);    // entry point id, unread
		words.insert(words.end(), packed.begin(), packed.end());
	}

	std::vector<std::byte> bytes(words.size() * sizeof(uint32_t));
	std::memcpy(bytes.data(), words.data(), bytes.size());
	return bytes;
}

}

// Slang renames a module's *only* entry point to "main". A shader with several keeps their declared names,
// so every pass in this engine asks for "vertexMain"/"computeMain" and that works right up until a module
// drops to one entry point - at which point pipeline creation fails with `entry point not found` and,
// if the pass is one the renderer needs, the editor dies on launch
//
// That has happened three times: sh_project.slang when it was written single-entry, then shadow_depth.slang,
// depth_prepass.slang and skinning.slang together the moment their vertexMainSkinned variants were deleted.
// Twice it was written down in the roadmap and the note did not prevent the third, so it is a test now
TOAST_TEST_NAMED("Renderer", "renderer/03-spirv-entry-points", test_renderer_03_spirv_entry_points) {
	using namespace renderer::spirv;

	// The multi-entry case: names survive, and each stage sees only its own
	const auto multi = makeModule(
	    {{ExecutionModel::vertex, "vertexMain"}, {ExecutionModel::fragment, "fragmentMain"}, {ExecutionModel::fragment, "fragmentMainCutout"}}
	);

	const auto vertex_names = entryPointNames(multi, ExecutionModel::vertex);
	assert(vertex_names.size() == 1);
	assert(vertex_names[0] == "vertexMain");

	const auto fragment_names = entryPointNames(multi, ExecutionModel::fragment);
	assert(fragment_names.size() == 2);
	assert(fragment_names[0] == "fragmentMain");
	assert(fragment_names[1] == "fragmentMainCutout");

	assert(entryPointNames(multi, ExecutionModel::compute).empty());

	// Present: returned unchanged
	assert(resolveEntryPoint(multi, ExecutionModel::vertex, "vertexMain", "test") == "vertexMain");

	// Absent with two candidates: nothing can be assumed, so the request stands and the pipeline fails
	// loudly rather than binding whichever entry point happened to be first
	assert(resolveEntryPoint(multi, ExecutionModel::fragment, "typo", "test") == "typo");

	// The single-entry case, which is the whole reason this exists
	const auto single = makeModule({{ExecutionModel::vertex, "main"}});
	assert(resolveEntryPoint(single, ExecutionModel::vertex, "vertexMain", "test") == "main");
	assert(resolveEntryPoint(single, ExecutionModel::compute, "computeMain", "test") == "computeMain");

	// A name that exactly fills its last word still needs its terminator word, and must not run into the
	// following instruction
	const auto exact = makeModule({{ExecutionModel::compute, "abcd"}, {ExecutionModel::compute, "wxyz"}});
	const auto exact_names = entryPointNames(exact, ExecutionModel::compute);
	assert(exact_names.size() == 2);
	assert(exact_names[0] == "abcd");
	assert(exact_names[1] == "wxyz");

	// Anything unreadable degrades to "use what you were asked for" rather than throwing or looping
	assert(entryPointNames({}, ExecutionModel::vertex).empty());

	std::vector<std::byte> not_spirv(64, std::byte {0xAB});
	assert(entryPointNames(not_spirv, ExecutionModel::vertex).empty());
	assert(resolveEntryPoint(not_spirv, ExecutionModel::vertex, "vertexMain", "test") == "vertexMain");

	auto header_only = makeModule({});
	assert(entryPointNames(header_only, ExecutionModel::vertex).empty());

	// A zero-length instruction would leave the cursor where it is; the parser must stop rather than spin
	auto malformed = makeModule({{ExecutionModel::vertex, "vertexMain"}});
	malformed.resize(malformed.size() + sizeof(uint32_t), std::byte {0});
	assert(entryPointNames(malformed, ExecutionModel::vertex).size() == 1);

	// An instruction claiming more words than the module holds must not be read past the end
	auto truncated = makeModule({{ExecutionModel::vertex, "vertexMain"}});
	const uint32_t oversized = (0xFFFFu << 16) | 15u;
	std::memcpy(truncated.data() + (5 * sizeof(uint32_t)), &oversized, sizeof(oversized));
	assert(entryPointNames(truncated, ExecutionModel::vertex).empty());
}
