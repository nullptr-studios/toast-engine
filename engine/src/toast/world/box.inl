#pragma once
#include "box.hpp"
#include <toast/log.hpp>

namespace toast {

template<NodeType T>
Box<T>::Box(Node* node) noexcept {
	m = {
	  .control = _detail::ControlBox::getControlBlock(node),
	};
}

template<NodeType T>
Box<T>::Box(Node* node, std::string_view path) noexcept {
	// this = node->find(path);
	// TODO:
	
}

template<NodeType T>
Box<T>::Box(const Box<T>& other) noexcept {
	m = {
	  .control = other.m.control,
	};
	if (m.control) {
		m.control->increment();
	}
}

template<NodeType T>
Box<T>::Box(Box<T>&& other) noexcept {
	m = {
	  .box = other.m.control,
	};
	other.m.control = nullptr;
}

template<NodeType T>
Box<T>::~Box() noexcept {
	if (m.control) {
		m.control->decrement();
		m.control = nullptr;
	}
}

template<NodeType T>
auto Box<T>::operator=(const Box<T>& other) noexcept -> Box<T>& {
	if (this != &other) {
		if (m.control) {
			m.control->decrement();
			m.control = nullptr;
		}
		m.control = other.m.control;
		if (m.control) {
			m.control->increment();
		}
	}
	return *this;
}

template<NodeType T>
auto Box<T>::operator=(Box<T>&& other) noexcept -> Box<T>& {
	if (this != &other) {
		if (m.control) {
			m.control->decrement();
			m.control = nullptr;
		}
		m.control = other.m.control;
		other.m.control = nullptr;
	}
	return *this;
}

template<NodeType T>
auto Box<T>::operator==(Box& other) const noexcept -> bool {
	return m.control == other.m.control;
}

template<NodeType T>
auto Box<T>::operator!=(Box& other) const noexcept -> bool {
	return not(m.control == other.m.control);
}

template<NodeType T>
Box<T>::operator bool() const noexcept {
	return exists();
}

template<NodeType T>
auto Box<T>::operator->() noexcept -> T* {
	TOAST_ASSERT(m.control && m.control->node, Box<T>, "Control Box or Node is NULLPTR");
	return m.control->node;
}

template<NodeType T>
auto Box<T>::operator->() const noexcept -> const T* {
	TOAST_ASSERT(m.control && m.control->node, Box<T>, "Control Box or Node is NULLPTR");
	return m.control->node;
}

template<NodeType T>
auto Box<T>::operator*() const noexcept -> const T& {
	TOAST_ASSERT(m.control && m.control->node, Box<T>, "Control Box or Node is NULLPTR");
	return *m.control->node;
}

template<NodeType T>
auto Box<T>::operator*() noexcept -> T& {
	TOAST_ASSERT(m.control && m.control->node, Box<T>, "Control Box or Node is NULLPTR");
	return *m.control->node;
}

template<NodeType T>
Box<T>::operator T&() noexcept {
	TOAST_ASSERT(m.control && m.control->node, Box<T>, "Control Box or Node is NULLPTR");
	return *m.control->node;
}

template<NodeType T>
auto Box<T>::exists() const noexcept -> bool {
	return m.control && m.control->node;
}

}    // namespace toast
