//
// Created by xein on 10/2/25.
//

#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

#include <Engine/Event/ListenerComponent.hpp>

namespace event {

ListenerComponent::~ListenerComponent() {
	if (m_events.empty()) {
		return;
	}

	// Removes references for this listener on the events
	TOAST_INFO("Unsubscribing from {0} events", m_events.size());
	// Lock to prevent races with other threads that might be iterating subscribers
	std::lock_guard<std::mutex> lock(s_eventMutex);
	for (auto* event_map : m_events | std::views::values) {
		for (auto it = event_map->begin(); it != event_map->end(); ++it) {
			if (it->second == this) {
				event_map->erase(it);
				break;
			}
		}
	}
}
#ifdef TOAST_EDITOR
void ListenerComponent::Inspector() {
	Component::Inspector();
	ImGui::Text("Listening to %i events", m_events.size());
	ImGui::Text("%i callbacks subscribed", m_callbacks.size());
}
#endif

}
