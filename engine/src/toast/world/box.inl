#pragma once
#include "box.hpp"
#include "toast/log.hpp"

namespace toast {

template<typename T>
Box<T>::~Box() noexcept {
	if (control) {
		control->decrement();
	}
}

template<typename T>
Box<T>::Box(_detail::ControlBox* control_box) noexcept {
	if (control_box) {
		this->control = control_box;
		control->increment();
	}
}

template<typename T>
Box<T>::Box(const T* node) noexcept {
	_detail::ControlBox* control_box = _detail::ControlBox::get(node);
	if (control_box) {
		control = control_box;
		control->increment();
	}
}

template<typename T>
Box<T>::Box(const T& node) noexcept {
	_detail::ControlBox* control_box = _detail::ControlBox::get(node);
	if (control_box) {
		control = control_box;
		control->increment();
	}
}

template<typename T>
Box<T>::Box(const Box& other) noexcept {
	_detail::ControlBox* control_box = other.control;
	if (control_box) {
		control = control_box;
		control->increment();
	}
}

template<typename T>
auto Box<T>::operator=(const Box& other) noexcept -> Box& {
	if (this->control == other.control) {
		return *this;
	}
	if (control) {
		control->decrement();
	}
	control = other.control;
	if (control) {
		control->increment();
	}
	return *this;
}

template<typename T>
Box<T>::Box(Box&& other) noexcept {
	control = other.control;
	other.control = nullptr;
}

template<typename T>
auto Box<T>::operator=(Box&& other) noexcept -> Box& {
	if (this->control == other.control) {
		return *this;
	}
	if (control) {
		control->decrement();
	}
	control = other.control;
	other.control = nullptr;
	return *this;
}

template<typename T>
template<typename U>
  requires std::convertible_to<U*, T*>
Box<T>::Box(const Box<U>& other) noexcept {
	_detail::ControlBox* control_box = other.control;
	if (control_box) {
		control = control_box;
		control->increment();
	}
}

template<typename T>
template<typename U>
  requires std::convertible_to<U*, T*>
auto Box<T>::operator=(const Box<U>& other) noexcept -> Box& {
	if (this->control == other.control) {
		return *this;
	}
	if (control) {
		control->decrement();
	}
	control = other.control;
	if (control) {
		control->increment();
	}
	return *this;
}

template<typename T>
template<typename U>
  requires std::convertible_to<U*, T*>
Box<T>::Box(Box<U>&& other) noexcept {
	control = other.control;
	other.control = nullptr;
}

template<typename T>
template<typename U>
  requires std::convertible_to<U*, T*>
auto Box<T>::operator=(Box<U>&& other) noexcept -> Box& {
	if (this->control == other.control) {
		return *this;
	}
	if (control) {
		control->decrement();
	}
	control = other.control;
	other.control = nullptr;
	return *this;
}

template<typename T>
auto Box<T>::exists() const noexcept -> bool {
	return (control != nullptr) && (control->node != nullptr);
}

template<typename T>
auto Box<T>::operator<=>(const Box& other) const noexcept -> std::strong_ordering {
	if (this->control && other.control) {
		return *this->control <=> *other.control;
	}
	return this->control <=> other.control;
}

template<typename T>
auto Box<T>::operator==(const Box& other) const noexcept -> bool {
	return (*this <=> other) == std::strong_ordering::equal;
}

template<typename T>
Box<T>::operator bool() const noexcept {
	return this->exists();
}

template<typename T>
Box<T>::operator T&() noexcept {
	TOAST_ASSERT(control && control->node, "Box", "Box is in a Invalid State: ControlBox or Node is nullptr");
	return static_cast<T&>(*control->node);
}

template<typename T>
auto Box<T>::operator->() noexcept -> T* {
	TOAST_ASSERT(control && control->node, "Box", "Box is in a Invalid State: ControlBox or Node is nullptr");
	return static_cast<T*>(control->node);
}

template<typename T>
auto Box<T>::operator*() noexcept -> T& {
	TOAST_ASSERT(control && control->node, "Box", "Box is in a Invalid State: ControlBox or Node is nullptr");
	return static_cast<T&>(*control->node);
}

template<typename T>
Box<T>::operator const T&() const noexcept {
	TOAST_ASSERT(control && control->node, "Box", "Box is in a Invalid State: ControlBox or Node is nullptr");
	return static_cast<T&>(*control->node);
}

template<typename T>
auto Box<T>::operator->() const noexcept -> const T* {
	TOAST_ASSERT(control && control->node, "Box", "Box is in a Invalid State: ControlBox or Node is nullptr");
	return static_cast<T*>(control->node);
}

template<typename T>
auto Box<T>::operator*() const noexcept -> const T& {
	TOAST_ASSERT(control && control->node, "Box", "Box is in a Invalid State: ControlBox or Node is nullptr");
	return static_cast<T&>(*control->node);
}

template<typename T>
template<typename U>
  requires std::derived_from<U, T>
auto Box<T>::as() const noexcept -> Box<U> {
	if (control && control->node) {
		if (dynamic_cast<const U*>(control->node)) {
			return Box<U>(control);
		}
	}
	return Box<U>();
}

template<typename T>
auto Box<T>::rid() const noexcept -> size_t {
	return control->rid();
}
}
