
#include <atomic>
#include <memory_resource>

namespace toast {
class Node;

struct ControlBox {
	std::atomic<unsigned int> ref_count;
	Node* node = nullptr;

	explicit operator bool() const { return node != nullptr; }

	void increment();

	void decrement();
};

class Box {
	ControlBox* m_box = nullptr;

public:
	Box() = default;

	// Copy Constructor
	Box(const Box& other);

	// Move Constructor
	Box(Box&& other) noexcept;

	~Box();

	// Copy Assignment
	auto operator=(const Box& other) -> Box&;

	// Move Assignment
	auto operator=(Box&& other) noexcept -> Box&;

	explicit operator bool() const;

	auto operator->() const -> Node*;

	auto operator*() const -> Node&;

	void release();

	[[nodiscard]]
	auto get() const -> Node&;

	[[nodiscard]]
	auto hasValue() const -> bool;
};

struct BoxMemPool {
	std::pmr::unsynchronized_pool_resource pool;

	BoxMemPool() : pool(std::pmr::pool_options({.max_blocks_per_chunk = 1024, .largest_required_pool_block = sizeof(ControlBox)})) { }

	auto createBox(Node* target) -> ControlBox*;

	void freeBox(ControlBox* box);
};

}
