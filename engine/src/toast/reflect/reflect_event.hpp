/**
 * @file reflect_event.hpp
 * @author Dante Harper
 * @date 03 Jul 26
 *
 * @brief [TODO: Brief description of the file's purpose]
 */

#pragma once
#include <asio/serial_port_base.hpp>
#include <toast/events/event.hpp>
#include <toast/export.hpp>
#include <toast/reflect/reflect.hpp>

namespace toast {

struct TOAST_API EventInfo {
	std::string_view name;
	std::type_index type;

	[[nodiscard]]
	constexpr auto id() const -> uint32_t {
		uint32_t hash = 2166136261u;
		for (char c : name) {
			hash ^= static_cast<uint8_t>(c);
			hash *= 16777619u;
		}
		return hash;
	}
};

class TOAST_API EventRegistry {
	static inline EventRegistry* instance = nullptr;
	std::unordered_map<std::string_view, const EventInfo*> types;

public:
	static void registerEvent(const EventInfo* info) { (*instance).types[info->name] = info; }

	[[nodiscard]]
	static auto reflect(std::string_view name) -> const EventInfo* {
		auto it = (*instance).types.find(name);
		return it != (*instance).types.end() ? it->second : nullptr;
	}
};
}

////////// reflection example //////////

template<>
struct toast::Reflect<event::_detail::IEvent> {
	static constexpr std::string_view name = "event::_detail::IEvent";
	inline static const toast::EventInfo type_info = {
	  .name = name,
	  .type = typeid(event::_detail::IEvent),
	};
};

void registerEventTypes() {
	static const std::array<const toast::EventInfo*, 8> all_types = {
	  &toast::Reflect<event::_detail::IEvent>::type_info,
	  &toast::Reflect<event::_detail::IEvent>::type_info,
	  &toast::Reflect<event::_detail::IEvent>::type_info,
	  &toast::Reflect<event::_detail::IEvent>::type_info,
	  &toast::Reflect<event::_detail::IEvent>::type_info,
	  &toast::Reflect<event::_detail::IEvent>::type_info,
	  &toast::Reflect<event::_detail::IEvent>::type_info,
	  &toast::Reflect<event::_detail::IEvent>::type_info,
	};
	for (const auto* info : all_types) {
		toast::EventRegistry::registerEvent(info);
	}
}

////////// reflection example //////////
