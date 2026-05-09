/**
 * @file event_system.hpp
 * @author Dante Harper
 * @date 09 May 26
 *
 * @brief Event System Singleton
 */

#pragma once

#include "toast/events/event.hpp"
#include "toast/export.hpp"

#include <any>
#include <memory>
#include <typeindex>

namespace event {
struct TOAST_API EventSystem {
	struct EventInfo {
		std::mutex mutex;
		std::multimap<char, void*, std::greater<>> callbacks;
		std::vector<void*> processing;
		bool cached = false;
	};

	/// @brief mutex for the event system pool
	static std::mutex pool_mutex;

	/// @brief vtable for unsubscribing callbacks
	static std::unordered_map<std::type_index, std::function<void(std::any)>> unsubscribe_map;

	/// @brief callbacks will be cleaned up only before pollEvents
	/// @note unique pointers have the option of storing a pointer to the function that deletes the object
	/// so thats how i implement the deletion_queue
	static std::vector<std::unique_ptr<void, void (*)(void*)>> deletion_queue;

	static std::unordered_map<std::type_index, EventInfo> event_data;


	template<typename T>
	static void registerEvent() {
		static_assert(std::is_base_of_v<Event<T>, T>, "CONTRACT VIOLATION: You Must Inhert as 'struct Derived : Event<Derived>'");
		TOAST_INFO(_detail::IEvent, "Registering Event Type: {}", typeid(T).name());

		EventSystem::unsubscribe_map.emplace(typeid(T), [](const std::any& iter) {
			auto it = std::any_cast<T::iterator_t>(iter);
			unsubscribe(it);
		});
	}
};
}
