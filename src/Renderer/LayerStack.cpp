//
// Created by dario on 15/09/2025.
//

#include <Engine/Core/Profiler.hpp>
#include <Engine/Renderer/LayerStack.hpp>

namespace renderer {

LayerStack* LayerStack::m_instance = nullptr;

LayerStack::LayerStack() {
	if (!m_instance) {
		m_instance = this;
	}
}

LayerStack::~LayerStack() {
	for (auto* layer : m_layers) {
		layer->OnDetach();
		delete layer;
	}
}

void LayerStack::PushLayer(ILayer* layer) {
	m_layers.emplace(m_layers.begin() + m_layerInsertIndex, layer);
	m_layerInsertIndex++;
	layer->OnAttach();
}

void LayerStack::PushOverlay(ILayer* overlay) {
	m_layers.emplace_back(overlay);
	overlay->OnAttach();
}

void LayerStack::PopLayer(ILayer* layer) {
	if (auto it = std::ranges::find(m_layers, layer); it != m_layers.end()) {
		layer->OnDetach();
		m_layers.erase(it);
	}
}

void LayerStack::PopOverlay(ILayer* overlay) {
	if (auto it = std::ranges::find(m_layers, overlay); it != m_layers.end()) {
		overlay->OnDetach();
		m_layers.erase(it);
	}
}

void LayerStack::TickLayers() {
	PROFILE_ZONE;
	for (ILayer* layer : m_layers) {
		layer->OnTick();
	}
}

void LayerStack::RenderLayers() {
	PROFILE_ZONE;
	for (ILayer* layer : m_layers) {
		layer->OnRender();
	}
}
}
