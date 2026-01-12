/// @file Layer.hpp
/// @author Dario
/// @date 15/09/25
/// @brief Base class for layers

#pragma once
#include "Toast/Event/Event.hpp"

namespace renderer {

///@class ILayer
///@brief Base class for render layers
class ILayer {
public:
	ILayer(std::string name = "Default Layer") : m_name(std::move(name)) { }

	virtual ~ILayer() = default;

	virtual void OnAttach() = 0;
	virtual void OnDetach() = 0;
	virtual void OnTick() = 0;
	virtual void OnRender() = 0;

private:
	std::string m_name;
};
}
