#include "box.hpp"

#include "node.hpp"
#include "toast/log.hpp"

#include <memory_resource>

namespace toast::_detail {
static std::pmr::synchronized_pool_resource pool({1024, sizeof(ControlBox)});

auto ControlBox::operator new(std::size_t size) -> void* {
	return _detail::pool.allocate(size);
}

void ControlBox::operator delete(void* ptr, std::size_t size) {
	_detail::pool.deallocate(ptr, size);
}

void ControlBox::increment() {
	TOAST_TRACE(ControlBox, "increment");
	ref_count.fetch_add(1, std::memory_order_relaxed);
}

void ControlBox::decrement() {
	TOAST_TRACE(ControlBox, "decrement");
	if (ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
		delete this;
	}
}

auto ControlBox::getControlBlock(Node* node) -> ControlBox* {
	if (not node->m.self) {
		TOAST_TRACE(ControlBox, "ControlBox Created and incremented");
		return new _detail::ControlBox(1, node);
	}
	node->m.self.m_box->increment();
	return node->m.self.m_box;

	return nullptr;
}
}
