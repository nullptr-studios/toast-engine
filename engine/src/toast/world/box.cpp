#include "box.hpp"

#include "toast/log.hpp"

void toast::ControlBox::increment() {
	ref_count.fetch_add(1, std::memory_order_relaxed);
}

void toast::ControlBox::decrement() {
	if (ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
		// handle deletion
	}
}

toast::Box::Box(const Box& other) : m_box(other.m_box) {
	if (m_box) {
		m_box->increment();
	}
}

toast::Box::Box(Box&& other) noexcept : m_box(other.m_box) {
	other.m_box = nullptr;
}

toast::Box::~Box() {
	release();
}

auto toast::Box::operator=(const Box& other) -> Box& {
	if (this != &other) {
		release();
		m_box = other.m_box;
		if (m_box) {
			m_box->increment();
		}
	}
	return *this;
}

auto toast::Box::operator=(Box&& other) noexcept -> Box& {
	if (this != &other) {
		release();
		m_box = other.m_box;
		other.m_box = nullptr;
	}
	return *this;
}

toast::Box::operator bool() const {
	return hasValue();
}

auto toast::Box::operator->() const -> Node* {
	TOAST_ASSERT(m_box && m_box->node, "Control Box Is NULL");
	return m_box->node;
}

auto toast::Box::operator*() const -> Node& {
	return get();
}

void toast::Box::release() {
	if (m_box) {
		m_box->decrement();
		m_box = nullptr;
	}
}

[[nodiscard]]
auto toast::Box::get() const -> Node& {
	TOAST_ASSERT(m_box && m_box->node, "Control Box Is NULL");
	return *(m_box->node);
}

[[nodiscard]]
auto toast::Box::hasValue() const -> bool {
	return m_box && m_box->node;
}

auto toast::BoxMemPool::createBox(Node* target) -> ControlBox* {
	void* mem = pool.allocate(sizeof(ControlBox), alignof(ControlBox));
	// Placement new: initializes the struct in the pool memory
	return new (mem) ControlBox {.ref_count = {1}, .node = target};
}

void toast::BoxMemPool::freeBox(ControlBox* box) {
	// Explicitly call destructor because of the std::atomic
	box->~ControlBox();
	pool.deallocate(box, sizeof(ControlBox), alignof(ControlBox));
}
