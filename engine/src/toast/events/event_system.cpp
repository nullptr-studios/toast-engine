#include "event_system.hpp"

namespace event {
std::unordered_map<std::type_index, EventSystem::EventInfo> EventSystem::event_data;
std::mutex EventSystem::pool_mutex;
std::unordered_map<std::type_index, std::function<void(std::any)>> EventSystem::unsubscribe_map;
std::vector<std::unique_ptr<void, void (*)(void*)>> EventSystem::deletion_queue;
}    // namespace event
