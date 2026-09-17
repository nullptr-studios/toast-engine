/**
 * @file spirv_entry_points.hpp
 * @author dario
 * @date 14/08/2026
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <toast/export.hpp>
#include <vector>

namespace renderer::spirv {

enum class ExecutionModel : uint32_t {
	vertex = 0,
	fragment = 4,
	compute = 5,
};

[[nodiscard]]
auto TOAST_API entryPointNames(std::span<const std::byte> spirv, ExecutionModel model) -> std::vector<std::string>;

/// @brief Slang renames the only entry point of a module to main
[[nodiscard]]
auto TOAST_API resolveEntryPoint(
    std::span<const std::byte> spirv, ExecutionModel model, const std::string& requested, std::string_view debug_name
) -> std::string;

}
