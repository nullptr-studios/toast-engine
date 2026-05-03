/**
 * @file box.hpp
 * @author Dante Harper
 * @date 30 Apr 26
 */

#pragma once

#include "toast/export.hpp"

#include <atomic>
#include <string_view>

namespace toast {
class Node;

template<typename T>
concept NodeType = std::is_base_of_v<Node, T>;

namespace _detail {
struct TOAST_API ControlBox {
	std::atomic<unsigned int> ref_count;
	Node* node = nullptr;

	explicit operator bool() const noexcept;

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

	struct {
		_detail::ControlBox* control = nullptr;
	} m;

public:
	Box() noexcept = default;                              // Constructor
	~Box() noexcept;                                       // Deconstructor
	Box(Node* node) noexcept;                              // Node Constructor
	Box(Node* node, std::string_view path) noexcept;       // Path Constructor
	Box(const Box& other) noexcept;                        // Copy Constructor
	Box(Box&& other) noexcept;                             // Move Constructor

	auto operator=(const Box& other) noexcept -> Box&;     // Copy Assignment
	auto operator=(Box&& other) noexcept -> Box&;          // Move Assignment

	auto operator==(Box& other) const noexcept -> bool;    // Move Assignment
	auto operator!=(Box& other) const noexcept -> bool;    // Move Assignment

	inline operator T&() noexcept;                         // Implicit Node& Conversion
	inline explicit operator bool() const noexcept;        // Explicit bool Conversion

	inline auto operator->() noexcept -> T*;
	inline auto operator->() const noexcept -> const T*;

	inline auto operator*() noexcept -> T&;
	inline auto operator*() const noexcept -> const T&;

	[[nodiscard]]
	auto exists() const noexcept -> bool;
};

}

#include "box.inl"
