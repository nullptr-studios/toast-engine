#pragma once
#include "Toast/Log.hpp"

#include <exception>
#include <string>

namespace toast {
class Node;

class BadNode final : public std::exception {
public:
	BadNode(Node* parent, std::string_view message) : m_object(parent), m_message(message) { }

	const char* what() const noexcept override {
		TOAST_ERROR("BadNode exception:\n{0}", m_message);
		return m_message.c_str();
	}

	const Node* object() const noexcept {
		return m_object;
	}

private:
	Node* m_object;
	std::string m_message;
};

}
