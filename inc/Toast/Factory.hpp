/// @file Factory.hpp
/// @date 6/2/2025
/// @author Xein
/// @brief Class in charge of Object creation (not anmymore)

#pragma once

namespace toast {

/// @class toast::Factory
/// This class was in charge of the creation of any kind of Object and assign it a unique ID
/// @note This class shouldn't be used by the end user but by the implementation of the Actor
/// tree structure or any other that works as some sort of middleware
class Factory {
public:
	Factory();

	static unsigned AssignId() noexcept {
		return m_instance->m_idCount++;
	}

private:
	static Factory* m_instance;
	unsigned m_idCount = 0;
};

}
