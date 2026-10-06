/**
 * @file signals.hpp
 * @author Dante Harper
 * @date 09 Sep 26
 */

#pragma once

#pragma once

#include "signal_types.hpp"
#include "toast/uid.hpp"
#include "toast/world/box.hpp"

#include <algorithm>
#include <any>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace toast {
class Node;
}

namespace signals {

template<typename... Args>
class Signal {
	static_assert(sizeof...(Args) <= 4, "signals::Signal supports at most four arguments");

	using callback_t = std::function<void(Args...)>;

	struct Connection {
		toast::UID uid;
		std::string identifier;
		ConnectionSource source = ConnectionSource::unknown;
		bool forwards_args = true;

		toast::Box<toast::Node> node;
		callback_t cb;
	};

	using ConnectionList = std::vector<Connection>;

	/**
	 * Copy on write
	 *
	 * Hhandlers run against a snapshot so the same signal never invalidates what is being iterated
	 */
	mutable std::mutex m_mutex;
	std::shared_ptr<const ConnectionList> m_connections = std::make_shared<const ConnectionList>();

	[[nodiscard]]
	auto snapshot() const -> std::shared_ptr<const ConnectionList> {
		std::scoped_lock lock(m_mutex);
		return m_connections;
	}

	template<typename Edit>
	void modify(Edit&& edit) {
		std::scoped_lock lock(m_mutex);
		auto copy = std::make_shared<ConnectionList>(*m_connections);
		edit(*copy);
		m_connections = std::move(copy);
	}

public:
	Signal() = default;

	Signal(const Signal& other) : m_connections(other.snapshot()) { }

	Signal(Signal&& other) noexcept : m_connections(other.snapshot()) { }

	auto operator=(const Signal& other) -> Signal& {
		if (this != &other) {
			auto other_connections = other.snapshot();
			std::scoped_lock lock(m_mutex);
			m_connections = std::move(other_connections);
		}
		return *this;
	}

	auto operator=(Signal&& other) noexcept -> Signal& { return *this = other; }

	~Signal() = default;

	template<typename F>
	  requires(std::is_invocable_r_v<void, F, Args...> || std::is_invocable_r_v<void, F>)
	void connect(toast::Node& node, F&& cb);

	void connect(
	    toast::Node& node, std::string_view identifier, ConnectionSource source = ConnectionSource::cpp, bool forwards_args = true
	);

	void disconnect(toast::Node& node, std::string_view identifier, ConnectionSource source = ConnectionSource::cpp);

	void clear(ConnectionSource source);

	[[nodiscard]]
	auto connections() const -> std::vector<ConnectionInfo>;

	void fire(const Args&... args);

	template<typename NodeType, auto MemberPtr>
	static auto get(void* signal) -> std::vector<ConnectionInfo>;

	template<typename NodeType, auto MemberPtr>
	static void
	    connect(void* signal, toast::Node& target, std::string_view identifier, ConnectionSource source, bool forwards_args);

	template<typename NodeType, auto MemberPtr>
	static void disconnect(void* signal, toast::Node& target, std::string_view identifier, ConnectionSource source);

	template<typename NodeType, auto MemberPtr>
	static void clear(void* signal, ConnectionSource source);

	template<typename NodeType, auto MemberPtr>
	static auto fire(void* signal, std::span<const std::any> args) -> bool;
};

}
#ifndef NODEFILE
#include <toast/events/signals.inl>
#endif
