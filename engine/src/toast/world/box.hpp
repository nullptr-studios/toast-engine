/**
 * @file box.hpp
 * @author Dante Harper
 * @date 30 Apr 26
 */

#pragma once

#include "toast/export.hpp"

#include <atomic>

namespace toast {
class Node;

template<typename T>
concept NodeType = std::is_base_of_v<Node, T>;

namespace _detail {
struct TOAST_API ControlBox {
	std::atomic<unsigned int> ref_count;
	Node* node = nullptr;

	explicit operator bool() const { return node != nullptr; }

	void increment();
	void decrement();

	auto operator new(std::size_t size) -> void*;
	void operator delete(void* ptr, std::size_t size);

	static auto getControlBlock(Node* node) -> ControlBox*;
};
}

template<NodeType T>
class TOAST_API Box {
	friend class Node;
	friend struct _detail::ControlBox;
	_detail::ControlBox* m_box = nullptr;

public:
	Box() = default;                                 // Constructor
	~Box();                                          // Deconstructor
	Box(Node* node);
	Box(const Box& other);                           // Copy Constructor
	Box(Box&& other) noexcept;                       // Move Constructor
	auto operator=(const Box& other) -> Box&;        // Copy Assignment
	auto operator=(Box&& other) noexcept -> Box&;    // Move Assignment

	explicit operator bool() const;
	auto operator->() -> T*;
	auto operator->() const -> const T*;

	void release();

	[[nodiscard]]
	auto hasValue() const -> bool;
};

}

#include "box.inl"
