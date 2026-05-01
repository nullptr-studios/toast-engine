#pragma once
#include "box.hpp"
#include "toast/log.hpp"

namespace toast {

template<NodeType T>
Box<T>::Box(Node* node) : m_box(_detail::ControlBox::getControlBlock(node)) { }

template<NodeType T>
Box<T>::Box(const Box<T>& other) : m_box(other.m_box) {
	if (m_box) {
		m_box->increment();
	}
}

template<NodeType T>
Box<T>::Box(Box<T>&& other) noexcept : m_box(other.m_box) {
	other.m_box = nullptr;
}

template<NodeType T>
Box<T>::~Box() {
	release();
}

template<NodeType T>
auto Box<T>::operator=(const Box<T>& other) -> Box<T>& {
	if (this != &other) {
		release();
		m_box = other.m_box;
		if (m_box) {
			m_box->increment();
		}
	}
	return *this;
}

template<NodeType T>
auto Box<T>::operator=(Box<T>&& other) noexcept -> Box<T>& {
	if (this != &other) {
		release();
		m_box = other.m_box;
		other.m_box = nullptr;
	}
	return *this;
}

template<NodeType T>
Box<T>::operator bool() const {
	return hasValue();
}

template<NodeType T>
auto Box<T>::operator->() -> T* {
	TOAST_ASSERT(m_box && m_box->node, "Control Box Is NULL");
	return m_box->node;
}

template<NodeType T>
auto Box<T>::operator->() const -> const T* {
	TOAST_ASSERT(m_box && m_box->node, "Control Box Is NULL");
	return m_box->node;
}

template<NodeType T>
void Box<T>::release() {
	if (m_box) {
		m_box->decrement();
		m_box = nullptr;
	}
}

template<NodeType T>
auto Box<T>::hasValue() const -> bool {
	return m_box && m_box->node;
}

}    // namespace toast
