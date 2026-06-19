/**
 * @file box.hpp
 * @author Dante Harper
 * @date 18 May 26
 *
 * @brief pseudo shared pointer for Nodes
 *
 * Intrusive: the ControlBox lives inside the Node allocation, so all handles
 * share one ref-count without an extra heap block. This also makes ref-counting
 * safe across DLL boundaries where std::shared_ptr's control block would differ.
 */

#pragma once

#include "control_box.hpp"

namespace toast {
class Node;

/**
 * @brief Intrusive reference-counted handle to a Node
 *
 * The ControlBox lives inside the Node allocation itself, so all Box<Node> handles that refer
 * to the same node share one reference count without any extra heap allocation. This design
 * also makes the ref-count safe across DLL boundaries, where std::shared_ptr's control block
 * would differ between modules.
 *
 * A Box is null (empty) when default-constructed or after the node has been freed. Always
 * call exists() before dereferencing.
 *
 * @tparam T A Node subtype
 * @see ControlBox, Node
 */
template<typename T>
class Box {
	template<typename U>
	friend class Box;
	friend struct _detail::ControlBox;
	_detail::ControlBox* control = nullptr;

public:
	/// Default-constructs an empty (null) box; does not allocate anything
	Box() noexcept = default;
	~Box() noexcept;

	/// Low-level constructor used internally by the node allocation path; prefer constructing from a Node& or Node*
	Box(_detail::ControlBox* control_box) noexcept;

	/// Constructs from a live node pointer; safe to call with null (produces an empty box); increments ref count
	Box(const T* node) noexcept;

	/// Constructs from a live node reference; increments ref count
	Box(const T& node) noexcept;

	/// Shares ownership with other; increments ref count
	Box(const Box& other) noexcept;
	/// Shares ownership with other; increments ref count
	auto operator=(const Box& other) noexcept -> Box&;

	/// Transfers ownership from other; source becomes empty without touching the ref count
	Box(Box&& other) noexcept;
	/// Transfers ownership from other; source becomes empty without touching the ref count
	auto operator=(Box&& other) noexcept -> Box&;

	/// Derived-type copy; only enabled when U* is implicitly convertible to T*; increments ref count
	template<typename U>
	  requires std::convertible_to<U*, T*>
	Box(const Box<U>& other) noexcept;

	/// Derived-type copy assignment; only enabled when U* is implicitly convertible to T*
	template<typename U>
	  requires std::convertible_to<U*, T*>
	auto operator=(const Box<U>& other) noexcept -> Box&;

	/// Derived-type move; transfers ownership without touching the ref count
	template<typename U>
	  requires std::convertible_to<U*, T*>
	Box(Box<U>&& other) noexcept;

	/// Derived-type move assignment
	template<typename U>
	  requires std::convertible_to<U*, T*>
	auto operator=(Box<U>&& other) noexcept -> Box&;

	/**
	 * @brief Whether this box holds a live node
	 * @return false if the box is empty or if the node has been freed by World::drainDestroyQueue();
	 *         always check exists() before dereferencing
	 */
	auto exists() const noexcept -> bool;

	auto operator<=>(const Box& other) const noexcept -> std::strong_ordering;
	auto operator==(const Box& other) const noexcept -> bool;
	explicit operator bool() const noexcept;

	operator T&() noexcept;
	auto operator->() noexcept -> T*;
	auto operator*() noexcept -> T&;

	operator const T&() const noexcept;
	auto operator->() const noexcept -> const T*;
	auto operator*() const noexcept -> const T&;

	/// Downcasts to a derived type; a null Box returns a null Box rather than throwing
	template<typename U>
	  requires std::derived_from<U, T>
	[[nodiscard]]
	auto as() const noexcept -> Box<U>;

	/// Raw identity of the underlying ControlBox as a pointer value; stable for the node's lifetime; used as a hash key
	auto rid() const noexcept -> size_t;
};

}

template<typename T>
struct std::hash<toast::Box<T>> {
	auto operator()(const toast::Box<T>& a) const noexcept -> std::size_t { return std::hash<std::uintptr_t> {}(a.rid()); }
};

#include "box.inl"
