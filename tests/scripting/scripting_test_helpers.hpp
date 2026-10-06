#pragma once

#include "toast/assets/script.hpp"
#include "toast/scripting/lua_state.hpp"
#include "toast/world/world_test_access.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

namespace toast::tests::scripting_tests {

/// One LuaState pool per test process; created lazily on first use
inline auto luaState() -> ::scripting::LuaState& {
	static auto state = ::scripting::LuaState::create();
	return *state;
}

/// Builds a Script asset handle from inline source; storage outlives the test
inline auto makeScript(std::string_view source, uint64_t uid = 1) -> assets::Handle<assets::Script> {
	static std::vector<std::unique_ptr<assets::Script>> storage;
	storage.push_back(std::make_unique<assets::Script>(std::vector<uint8_t>(source.begin(), source.end())));
	return {storage.back().get(), toast::UID {uid}, "test://script.lua"};
}

/// Owns an interpreter on another thread until released, like a worker running a script on it
class InterpreterHolder {
public:
	explicit InterpreterHolder(size_t index) {
		m_thread = std::thread([this, index] {
			auto guard = ::scripting::LuaState::get().lock(index);
			m_held = true;
			while (!m_release) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		});
		while (!m_held) {
			std::this_thread::yield();
		}
	}

	void release() {
		m_release = true;
		if (m_thread.joinable()) {
			m_thread.join();
		}
	}

	~InterpreterHolder() { release(); }

	InterpreterHolder(const InterpreterHolder&) = delete;
	auto operator=(const InterpreterHolder&) -> InterpreterHolder& = delete;

private:
	std::atomic<bool> m_held {false};
	std::atomic<bool> m_release {false};
	std::thread m_thread;
};
}
