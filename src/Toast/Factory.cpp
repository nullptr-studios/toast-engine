#include "Toast/Factory.hpp"

#include "Toast/Log.hpp"

namespace toast {
Factory* Factory::m_instance = nullptr;

Factory::Factory() {
	if (m_instance != nullptr) {
		throw ToastException("Trying to create factory but it already exists");
	}
	m_instance = this;
}

}
