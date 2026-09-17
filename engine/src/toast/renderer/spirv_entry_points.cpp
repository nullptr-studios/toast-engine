/**
 * @file spirv_entry_points.cpp
 * @author dario
 * @date 14/08/2026
 */

#include "spirv_entry_points.hpp"

#include <algorithm>
#include <cstring>
#include <toast/log.hpp>

namespace renderer::spirv {

namespace {

constexpr uint32_t k_spirv_magic = 0x07230203;
/// magic version generator bound schema
constexpr uint32_t k_header_words = 5;
constexpr uint32_t k_op_entry_point = 15;
/// opcode execution model id then the name
constexpr uint32_t k_entry_point_name_word = 3;

}

auto entryPointNames(std::span<const std::byte> spirv, ExecutionModel model) -> std::vector<std::string> {
	std::vector<std::string> names;

	const size_t word_count = spirv.size_bytes() / sizeof(uint32_t);
	if (word_count <= k_header_words || (spirv.size_bytes() % sizeof(uint32_t)) != 0) {
		return names;
	}

	const auto* words = reinterpret_cast<const uint32_t*>(spirv.data());
	if (words[0] != k_spirv_magic) {
		return names;
	}

	for (size_t i = k_header_words; i < word_count;) {
		const uint32_t instruction_words = words[i] >> 16;
		const uint32_t opcode = words[i] & 0xFFFFu;

		if (instruction_words == 0 || i + instruction_words > word_count) {
			break;
		}

		if (opcode == k_op_entry_point && instruction_words > k_entry_point_name_word &&
		    static_cast<ExecutionModel>(words[i + 1]) == model) {
			const auto* chars = reinterpret_cast<const char*>(&words[i + k_entry_point_name_word]);
			const size_t max_bytes = (instruction_words - k_entry_point_name_word) * sizeof(uint32_t);
			names.emplace_back(chars, ::strnlen(chars, max_bytes));
		}

		i += instruction_words;
	}

	return names;
}

auto resolveEntryPoint(
    std::span<const std::byte> spirv, ExecutionModel model, const std::string& requested, std::string_view debug_name
) -> std::string {
	const auto names = entryPointNames(spirv, model);
	if (names.empty() || std::ranges::find(names, requested) != names.end()) {
		return requested;
	}

	if (names.size() == 1) {
		TOAST_TRACE(
		    "Render", "{}: entry point '{}' not in SPIR-V; using the module's only one, '{}'", debug_name, requested, names[0]
		);
		return names[0];
	}

	TOAST_ERROR(
	    "Render",
	    "{}: entry point '{}' not found and the module declares {} for this stage, so none can be assumed",
	    debug_name,
	    requested,
	    names.size()
	);
	return requested;
}

}
