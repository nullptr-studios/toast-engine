/**
 * @file control_box.hpp
 * @author Dante Harper
 * @date 18 May 26
 *
 * @brief Control Box for sudo shared pointer Box<Node> class
 */

#pragma once

#include <toast/export.hpp>
#include <cstdint>

namespace toast {
class Node;

namespace _detail {
struct TOAST_API ControlBox {
	std::atomic<unsigned int> ref_count;
	Node* node = nullptr;

	ControlBox(const ControlBox&) = delete;
	ControlBox(ControlBox&&) = delete;
	auto operator=(const ControlBox&) -> ControlBox& = delete;
	auto operator=(ControlBox&&) -> ControlBox& = delete;

	explicit operator bool() const noexcept;
	auto operator<=>(const ControlBox& other) const noexcept -> std::strong_ordering;

	[[nodiscard]]
	auto rid() const noexcept -> std::uintptr_t;
	void increment();
	void decrement();

	static auto get(const Node* node) -> ControlBox*;
	static auto get(const Node& node) -> ControlBox*;
};

}
}

