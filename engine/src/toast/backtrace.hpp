/**
 * @file backtrace.hpp
 * @author Xein
 * @date 24 Oct 2026
 * @brief Call stacks attached to log records
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace logging {

struct RawBacktrace {
	static constexpr std::size_t k_capture_depth = 20;
	std::array<std::uintptr_t, k_capture_depth> frames {};
	std::uint8_t count = 0;
};

auto captureBacktrace(std::size_t skip = 0) noexcept -> RawBacktrace;

auto formatBacktrace(const RawBacktrace& trace) -> std::string;

void warmUpBacktrace() noexcept;

}
