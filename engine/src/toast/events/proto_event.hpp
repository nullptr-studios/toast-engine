/**
 * @file proto_event.hpp
 * @author Xein
 * @date 15 Jun 2026
 *
 * @brief FFI glue between native events and their protobuf
 */
#pragma once

#include "event.hpp"
#include "ffi/events.h"
#include "listener.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace event {

namespace _detail {

/**
 * @brief Dispatch table for one protobuf-backed event type over the FFI
 *
 * Populated by registerProtoEvent<T>() and stored in the proto registry.
 * The three function pointers cover the three directions of the FFI bridge:
 * sending from C#, subscribing from C#, and cancelling a subscription.
 */
struct ProtoEntry {
	int (*send)(const uint8_t* data, uint32_t size);      ///< C# -> engine: deserializes bytes and dispatches the event
	void (*subscribe)(
	    Listener&, event_callback, void* user_data, char priority, const char* label
	);                                                    ///< engine -> C#: serializes the event and calls the C# callback
	void (*unsubscribe)(Listener&, const char* label);    ///< cancels a subscription by label
};

/// Inserts a ProtoEntry into the global proto registry under the protobuf message name
void registerProtoEntry(std::string name, ProtoEntry entry);

}

template<ExposedEvent T>
void registerProtoEvent() {
	using Traits = ProtoTraits<T>;
	using Proto = typename Traits::Proto;

	EventSystem::registerEvent<T>();

	_detail::ProtoEntry entry {
	  .send = [](const uint8_t* data, uint32_t size) -> int {
		  Proto p;
		  if (not p.ParseFromArray(data, static_cast<int>(size))) {
			  return -2;
		  }
		  event::send<T>(Traits::fromProto(p));
		  return 0;
	  },
	  .subscribe =
	      [](Listener& l, event_callback callback, void* user_data, char priority, const char* label) {
		      l.subscribe<T>(
		          std::string {label},
		          [callback, user_data](T& ev) -> bool {
			          try {
				          Proto p = Traits::toProto(ev);
				          std::string bytes = p.SerializeAsString();
				          std::string name {Proto::descriptor()->full_name()};
				          return callback(
				                     name.c_str(),
				                     reinterpret_cast<const uint8_t*>(bytes.data()),
				                     static_cast<uint32_t>(bytes.size()),
				                     user_data
				                 ) != 0;
			          } catch (const std::exception& e) {
				          // swallow serialization failures so one bad event doesn't kill the whole chain
				          TOAST_WARN("Events", "Failed to serialize event {}: {}", Proto::descriptor()->full_name(), e.what());
				          return false;
			          }
		          },
		          priority
		      );
	      },
	  .unsubscribe = [](Listener& l, const char* label) { l.unsubscribe<T>(std::string_view {label}); },
	};

	_detail::registerProtoEntry(std::string {Proto::descriptor()->full_name()}, entry);
}

namespace _detail {

/// Returns the global list of proto registration callbacks; each TOAST_PROTO_EVENT() macro appends one entry
inline auto protoRegistrars() -> std::vector<void (*)()>& {
	static std::vector<void (*)()> registrars;
	return registrars;
}

/// each TOAST_PROTO_EVENT() macro creates a static instance of this; the constructor queues the
/// registration so registerProtoEvents() can flush them all in one call at startup
template<ExposedEvent T>
struct ProtoAutoRegister {
	ProtoAutoRegister() {
		protoRegistrars().push_back([] { registerProtoEvent<T>(); });
	}
};

}

}

#define TOAST_PROTO_CAT_(a, b) a##b
#define TOAST_PROTO_CAT(a, b) TOAST_PROTO_CAT_(a, b)

/// @brief Place next to a @c ProtoTraits<EventType> specialization to expose it over the FFI.
#define TOAST_PROTO_EVENT(EventType)                                                                      \
	static const ::event::_detail::ProtoAutoRegister<EventType> TOAST_PROTO_CAT(_proto_auto_, __LINE__) { }
