//
// Created by dario on 15/09/2025.
//

#include "Toast/Renderer/LayerStack.hpp"

#include "Toast/Profiler.hpp"

namespace renderer {

LayerStack* LayerStack::m_instance = nullptr;

LayerStack::LayerStack() {
	PROFILE_ZONE_N("LayerStack Construction");
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
	PROFILE_ZONE;
	m_layers.emplace(m_layers.begin() + m_layerInsertIndex, layer);
	m_layerInsertIndex++;
	layer->OnAttach();
}

void LayerStack::PushOverlay(ILayer* overlay) {
	PROFILE_ZONE;
	m_layers.emplace_back(overlay);
	overlay->OnAttach();
}

void LayerStack::PopLayer(ILayer* layer) {
	PROFILE_ZONE;
	if (auto it = std::ranges::find(m_layers, layer); it != m_layers.end()) {
		layer->OnDetach();
		m_layers.erase(it);
	}
}

void LayerStack::PopOverlay(ILayer* overlay) {
	PROFILE_ZONE;
	if (auto it = std::ranges::find(m_layers, overlay); it != m_layers.end()) {
		overlay->OnDetach();
		m_layers.erase(it);
	}
}

void LayerStack::TickLayers() const {
	PROFILE_ZONE;
	for (ILayer* layer : m_layers) {
		layer->OnTick();
	}
}

void LayerStack::RenderLayers() const {
	PROFILE_ZONE;
	for (ILayer* layer : m_layers) {
		layer->OnRender();
	}
}
}
