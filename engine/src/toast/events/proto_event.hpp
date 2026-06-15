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

struct ProtoEntry {
	int (*send)(const uint8_t* data, uint32_t size);                                                    ///< C# -> engine
	void (*subscribe)(Listener&, event_callback, void* user_data, char priority, const char* label);    ///< engine -> C#
	void (*unsubscribe)(Listener&, const char* label);                                                  ///< drop a subscription
};

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
			          } catch (...) { return false; }
		          },
		          priority
		      );
	      },
	  .unsubscribe = [](Listener& l, const char* label) { l.unsubscribe<T>(std::string_view {label}); },
	};

	_detail::registerProtoEntry(std::string {Proto::descriptor()->full_name()}, entry);
}

namespace _detail {

inline auto protoRegistrars() -> std::vector<void (*)()>& {
	static std::vector<void (*)()> registrars;
	return registrars;
}

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
