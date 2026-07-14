/**
 * @file lua_state.hpp
 * @author Xein
 * @date 10 Jul 2026
 *
 * @brief Pool of independent Lua interpreters
 */

#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <toast/export.hpp>
#include <toast/thread_pool.hpp>

struct lua_State;

namespace scripting {

class TOAST_API LuaState {
public:
	static constexpr size_t pool_size = 1 + toast::ThreadPool::thread_count;

	class Lock {
	public:
		Lock() = default;
		Lock(Lock&& other) noexcept;
		auto operator=(Lock&& other) noexcept -> Lock&;
		Lock(const Lock&) = delete;
		auto operator=(const Lock&) -> Lock& = delete;
		~Lock();

		explicit operator bool() const noexcept { return m_lock.owns_lock(); }

		[[nodiscard]]
		auto state() const noexcept -> lua_State* {
			return m_state;
		}

	private:
		friend class LuaState;
		Lock(std::unique_lock<std::recursive_timed_mutex> lock, lua_State* state, size_t index) noexcept;

		std::unique_lock<std::recursive_timed_mutex> m_lock;
		lua_State* m_state = nullptr;
		size_t m_index = 0;
	};

	static auto create() noexcept -> std::unique_ptr<LuaState>;
	static auto get() noexcept -> LuaState&;

	[[nodiscard]]
	static auto exists() noexcept -> bool {
		return instance != nullptr;
	}

	~LuaState() noexcept;

	/**
	 * @brief Acquires the state at `index` for the current thread
	 *
	 * Blocks if the thread holds no other pooled state
	 */
	[[nodiscard]]
	auto lock(size_t index) noexcept -> Lock;

	/// Non-blocking variant of lock()
	[[nodiscard]]
	auto tryLock(size_t index) noexcept -> Lock;

	void plotMemory() noexcept;

	[[nodiscard]]
	auto nextIndex() noexcept -> size_t;

	/// @brief Runs a chunk on state 0; intended for debug/console use
	auto runString(std::string_view lua_code) noexcept -> bool;

	/// Re-registers the Node/Asset type-marker globals on every state
	void refreshTypeMarkers() noexcept;

private:
	static inline LuaState* instance = nullptr;

	struct Entry {
		lua_State* state = nullptr;
		std::recursive_timed_mutex mutex;
	};

	std::array<Entry, pool_size> m_entries;
	std::atomic<size_t> m_next_index = 0;

	LuaState();

	static void registerApi(lua_State* state) noexcept;
	static void registerTypeMarkers(lua_State* state) noexcept;
};

}
