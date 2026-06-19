/**
 * @file control_box.hpp
 * @author Dante Harper
 * @date 18 May 26
 *
 * @brief Control block for the pseudo shared pointer Box<Node>
 */

#pragma once

#include <atomic>
#include <compare>
#include <cstdint>
#include <functional>
#include <toast/export.hpp>

namespace toast {
class Node;

namespace _detail {
struct WorldTestAccess;

/**
 * @brief Reference-count control block embedded inside the Node allocation
 *
 * Lives immediately before the Node in a single contiguous heap block so that
 * Box<Node> handles need no separate allocation. ControlBox is an implementation
 * detail; user code should interact exclusively through Box<Node>.
 *
 * @note Copy and move are deleted: the ControlBox must never move in memory
 *       because all Box handles hold raw pointers to it
 */
struct TOAST_API ControlBox {
	std::atomic<unsigned int> ref_count;    ///< starts at 1 (the node's self-reference); node is freed when it hits 0
	Node* node = nullptr;                   ///< null once the node is destroyed

	/// Initializes ref_count to 1 and stores the node pointer; called by nodeAllocation()
	ControlBox(Node* n);

	ControlBox(const ControlBox&) = delete;
	ControlBox(ControlBox&&) = delete;
	auto operator=(const ControlBox&) -> ControlBox& = delete;
	auto operator=(ControlBox&&) -> ControlBox& = delete;

	/// Returns true when the node pointer is non-null, i.e. the node has not been freed
	explicit operator bool() const noexcept;

	/// Total order over ControlBox identity; compares by rid() so boxes sort consistently in maps
	auto operator<=>(const ControlBox& other) const noexcept -> std::strong_ordering;
	auto operator==(const ControlBox& other) const noexcept -> bool;

	/**
	 * @brief Stable numeric identity for this ControlBox
	 * @return The ControlBox address cast to uintptr_t; stable for the lifetime of the node;
	 *         used as a hash key in the dependency graph maps
	 */
	[[nodiscard]]
	auto rid() const noexcept -> std::uintptr_t;
	/// relaxed acquire; acquiring a Box is always preceded by an acquire elsewhere
	void increment();
	/// acq_rel release; ensures the last releaser sees all prior writes before decrementing
	void decrement();

	/// Recovers the ControlBox from a raw node pointer; the ControlBox sits immediately before the Node in memory
	static auto get(const Node* node) -> ControlBox*;
	/// Recovers the ControlBox from a node reference; equivalent to get(&node)
	static auto get(const Node& node) -> ControlBox*;
};

}
}

template<>
struct std::hash<toast::_detail::ControlBox> {
	auto operator()(const toast::_detail::ControlBox& a) const noexcept -> std::size_t {
		return std::hash<std::uintptr_t> {}(a.rid());
	}
};
