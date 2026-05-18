#include "control_box.hpp"

#include "node.hpp"

namespace toast::_detail {

ControlBox::operator bool() const noexcept {
	return node;
}

auto ControlBox::operator<=>(const ControlBox& other) const noexcept -> std::strong_ordering {
	return rid() <=> other.rid();
}

auto ControlBox::operator==(const ControlBox& other) const noexcept -> bool {
	return rid() == other.rid();
}

auto ControlBox::rid() const noexcept -> std::uintptr_t {
	return reinterpret_cast<std::uintptr_t>(this);
}

void ControlBox::increment() {
	ref_count.fetch_add(1, std::memory_order_relaxed);
}

void ControlBox::decrement() {
	if (ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
		// TODO: handle deletion
	}
}

auto ControlBox::get(const Node* node) -> ControlBox* {
	return node->m.box.control;
}

auto ControlBox::get(const Node& node) -> ControlBox* {
	return node.m.box.control;
}

}
