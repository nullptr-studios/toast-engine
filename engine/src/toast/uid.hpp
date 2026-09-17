/**
 * @file uid.hpp
 * @author Xein
 * @date 29 Apr 2026
 *
 * @brief Unique identifier for everything that exists in our engine
 */

#pragma once

#include <format>
#include <toast/export.hpp>

namespace assets {
class Prefab;
}

namespace toast {

struct TOAST_API UID {
	friend class Engine;
	friend class INodeOwner;
	friend class assets::Prefab;

	UID();

	UID(uint64_t val) : value(val) { }

	/**
	 * @brief Implicit conversion to `std::string`
	 * @note Allocates; prefer `get()` when you want to pass the result to `std::format`
	 */
	operator std::string() const noexcept;
	auto operator<=>(const UID& other) const noexcept -> std::strong_ordering;
	auto operator==(const UID& other) const noexcept -> bool;

	/**
	 * @brief Encodes the 64-bit value as an 11-character base64url string
	 * @return The canonical text representation, e.g. "aB3xY_k9mZw"
	 */
	[[nodiscard]]
	auto get() const noexcept -> std::string;

	/// Returns the raw 64-bit integer; useful for hashing or serialization that owns the encoding
	[[nodiscard]]
	auto data() const noexcept -> uint64_t;

	/// Encodes any 64-bit value as an 11-character base64url string; same encoding as get()
	static auto toString(uint64_t uid) -> std::string;

	/// Decodes an 11-character base64url string produced by toString(); inverse of toString()
	static auto fromString(std::string_view b64) -> uint64_t;

	/// Generates and returns a fresh unique UID using the same algorithm as the node registry
	static auto make() -> UID;

private:
	void generate();    ///< lower 42 bits = timestamp, upper 22 bits = atomic counter mixed with a hash; gives ~4M unique IDs per
	                    ///< millisecond
	void assign(
	    std::string_view b64
	);                  ///< for deserialization only; base64url-decodes the 11-char string back to the 64-bit value
	uint64_t value;
};

}

/// enables std::format("{}", uid), delegates to the base string formatter
template<>
struct std::formatter<toast::UID> : std::formatter<std::string> {
	auto format(const toast::UID& uid, std::format_context& ctx) const {
		return std::formatter<std::string>::format(uid.get(), ctx);
	}
};
