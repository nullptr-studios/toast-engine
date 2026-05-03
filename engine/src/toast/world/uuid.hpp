/**
 * @file uuid.hpp
 * @author Xein
 * @date 29 Apr 26
 *
 * @brief Unique identifier for everything that exists in our engine
 */

#pragma once
#include <cstdint>
#include <string_view>
#include <toast/export.hpp>

namespace toast {

struct TOAST_API UUID {
	UUID();
	UUID(std::string_view value);
	auto operator=(std::string_view value) -> UUID&;

	operator uint64_t() const noexcept;
	operator std::string() const noexcept;
	inline auto operator==(UUID& other) const noexcept -> bool;
	inline auto operator!=(UUID& other) const noexcept -> bool;

	[[nodiscard]]
	auto get() const noexcept -> std::string;

	[[nodiscard]]
	auto data() const noexcept -> uint64_t;

	inline static auto toString(uint64_t uuid) -> std::string;
	inline static auto fromString(std::string_view b64) -> uint64_t;

private:
	uint64_t value;
};

}
