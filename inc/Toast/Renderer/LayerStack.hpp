//
// Created by dario on 15/09/2025.
//

#pragma once
#include "ILayer.hpp"

#include <vector>

namespace renderer {

///@class LayerStack
///@brief Manages a stack of layers and overlays
class LayerStack {
public:
	LayerStack();
	~LayerStack();

	///@brief Pushes a layer to the stack
	void PushLayer(renderer::ILayer* layer);
	///@brief Pushes an overlay to the stack (always on top)
	void PushOverlay(renderer::ILayer* overlay);
	///@brief Pops a layer from the stack
	void PopLayer(renderer::ILayer* layer);
	///@brief Pops an overlay from the stack
	void PopOverlay(renderer::ILayer* overlay);

	void TickLayers();
	void RenderLayers();

	const std::vector<renderer::ILayer*>& GetLayers() {
		return m_layers;
	}

	static LayerStack* GetInstance() {
		return m_instance;
	}

private:
	static LayerStack* m_instance;
	int m_layerInsertIndex = 0;
	std::vector<renderer::ILayer*> m_layers;
};
}
