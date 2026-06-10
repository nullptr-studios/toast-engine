/**
 * @file uid.hpp
 * @author Xein
 * @date 29 Apr 2026
 *
 * @brief Unique identifier for everything that exists in our engine
 */

#pragma once

#include "toast/export.hpp"

#include <format>

namespace toast {

struct TOAST_API UID {
	friend class World;
	friend class NodeFile;
	
	UID();
	UID(uint64_t val) : value(val) { }

	/**
	 * @brief Implicit cast to `std::string`
	 * @note This will make a heap allocation as it creates a new `std::string`
	 */
	operator std::string() const noexcept;
	auto operator<=>(const UID& other) const noexcept -> std::strong_ordering;

	[[nodiscard]]
	auto get() const noexcept -> std::string;

	[[nodiscard]]
	auto data() const noexcept -> uint64_t;

	inline static auto toString(uint64_t uid) -> std::string;
	inline static auto fromString(std::string_view b64) -> uint64_t;

private:
	void generate();                      ///< @brief Creates a new UID
	void assign(std::string_view b64);    ///< @brief Loads a UID from serialization
	uint64_t value;
};

}

template<>
struct std::formatter<toast::UID> : std::formatter<std::string> {
	auto format(const toast::UID& uid, std::format_context& ctx) const {
		// Forwards the string representation to the base std::string formatter
		return std::formatter<std::string>::format(uid.get(), ctx);
	}
};
