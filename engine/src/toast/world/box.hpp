/**
 * @file box.hpp
 * @author Dante Harper
 * @date 18 May 26
 *
 * @brief sudo shared pointer class for the Nodes
 */

#pragma once

#include "control_box.hpp"

namespace toast {
class Node;
template<typename T>
concept NodeConcept = std::derived_from<T, Node>;

template<NodeConcept T>
class Box {
	template<NodeConcept U>
	friend class Box;
	friend struct _detail::ControlBox;
	_detail::ControlBox* control = nullptr;

public:
	~Box() noexcept;                                      // Deconstructor

	Box(_detail::ControlBox* control_box) noexcept;       // ControlBox Constructor
	Box(const T* node) noexcept;                          // Node Constructor
	Box(const T& node) noexcept;                          // Node Constructor

	Box(const Box& other) noexcept;                       // Copy Constructor
	auto operator=(const Box& other) noexcept -> Box&;    // Copy Assignment

	Box(Box&& other) noexcept;                            // Move Constructor
	auto operator=(Box&& other) noexcept -> Box&;         // Move Assignment

	template<NodeConcept U>
	  requires std::convertible_to<U*, T*>
	Box(const Box<U>& other) noexcept;    // Derived Copy Constructor

	template<NodeConcept U>
	  requires std::convertible_to<U*, T*>
	auto operator=(const Box<U>& other) noexcept -> Box&;    // Derived Copy Assignment

	template<NodeConcept U>
	  requires std::convertible_to<U*, T*>
	Box(Box<U>&& other) noexcept;    // Derived Move Constructor

	template<NodeConcept U>
	  requires std::convertible_to<U*, T*>
	auto operator=(Box<U>&& other) noexcept -> Box&;                              // Derived Move Assigment

	auto operator<=>(const Box& other) const noexcept -> std::strong_ordering;    // Spaceship Operator
	explicit operator bool() const noexcept;                                      // Bool Conversion

	operator T&() noexcept;                                                       // Implicit Node& Conversion
	auto operator->() noexcept -> T*;                                             // Arrow Operator
	auto operator*() noexcept -> T&;                                              // Derefernce Operator

	operator const T&() const noexcept;                                           // Const Implicit Node& Conversion
	auto operator->() const noexcept -> const T*;                                 // Const Arrow Operator
	auto operator*() const noexcept -> const T&;                                  // Const Derefernce Operator

	template<NodeConcept U>
	  requires std::derived_from<U, T>
	[[nodiscard]]
	auto as() const noexcept -> Box<U>;
};

}

#include "box.inl"
