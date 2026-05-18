/**
 * @file uuid.hpp
 * @author Xein
 * @date 29 Apr 2026
 *
 * @brief Unique identifier for everything that exists in our engine
 */

#pragma once

#include "toast/export.hpp"

namespace toast {

struct TOAST_API UUID {
	UUID();
	UUID(std::string_view value);
	auto operator=(std::string_view value) -> UUID&;

	operator std::string() const noexcept;
	auto operator<=>(const UUID& other) const noexcept -> std::strong_ordering;

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

template <>
struct std::formatter<toast::UUID> : std::formatter<std::string> {
	auto format(const toast::UUID& uuid, std::format_context& ctx) const {
		// Forwards the string representation to the base std::string formatter
		return std::formatter<std::string>::format(uuid.get(), ctx);
	}
};
