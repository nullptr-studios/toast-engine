#pragma once
#include "Toast/Log.hpp"

#include <exception>
#include <string>

namespace toast {
class Object;

class BadObject final : public std::exception {
public:
	BadObject(Object* parent, const std::string& message) : m_object(parent), m_message(message) { }

	const char* what() const noexcept override {
		TOAST_ERROR("BadObject exception:\n{0}", m_message);
		return m_message.c_str();
	}

	const Object* object() const noexcept {
		return m_object;
	}

private:
	Object* m_object;
	std::string m_message;
};

}
