/**
 * @file lua_state.hpp
 * @author Xein
 * @date 10 Jul 2026
 *
 * @brief Pool of independent Lua interpreters
 *
 * A Lua interpreter is not thread safe, so every interpreter has exactly one owner thread at a time
 * and only the owner may touch its stack, its registry or any reference that lives inside it.
 * Ownership is taken through LuaState::lock() and is recursive for the owning thread
 *
 * The rules that keep this deadlock free:
 *  - a thread waits for an interpreter only while it owns no other interpreter
 *  - a thread that owns one and finds another one busy gets an empty Lock and must hand the work
 *    to ScriptDispatch instead of waiting
 *  - nothing that runs while an interpreter is owned may wait for another thread (futures, joins)
 *  - Lua values are released through retire(), never from a destructor that may run on a thread
 *    that does not own the interpreter
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <toast/export.hpp>
#include <toast/log.hpp>
#include <toast/thread_pool.hpp>
#include <vector>

struct lua_State;

namespace scripting {

class TOAST_API LuaState {
	struct Entry;

public:
	/// Ownership of one interpreter for as long as the object lives
	class TOAST_API Lock {
	public:
		Lock() = default;
		Lock(Lock&& other) noexcept;
		auto operator=(Lock&& other) noexcept -> Lock&;
		Lock(const Lock&) = delete;
		auto operator=(const Lock&) -> Lock& = delete;
		~Lock();

		explicit operator bool() const noexcept { return m_entry != nullptr; }

		[[nodiscard]]
		auto state() const noexcept -> lua_State* {
			return m_state;
		}

		[[nodiscard]]
		auto index() const noexcept -> size_t {
			return m_index;
		}

		/// Gives the interpreter back before the end of the scope
		void unlock() noexcept;

	private:
		friend class LuaState;
		Lock(Entry* entry, lua_State* state, size_t index) noexcept;

		Entry* m_entry = nullptr;
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
	 * @brief Acquires the interpreter at `index` for the calling thread
	 *
	 * Waits for the interpreter only when the calling thread owns no other one. A thread that already owns
	 * an interpreter never waits for a second one, because two threads holding them the other way round
	 * would deadlock, so the result is empty when the interpreter is busy
	 */
	[[nodiscard]]
	auto lock(size_t index) noexcept -> Lock;

	/// Non-blocking variant of lock()
	[[nodiscard]]
	auto tryLock(size_t index) noexcept -> Lock;

	/// Index of the pooled interpreter a state belongs to; works on coroutines too
	[[nodiscard]]
	static auto indexOf(lua_State* state) noexcept -> std::optional<size_t>;

	/// True when the calling thread currently owns the interpreter
	[[nodiscard]]
	static auto ownedByCurrentThread(size_t index) noexcept -> bool;
	[[nodiscard]]
	static auto ownedByCurrentThread(lua_State* state) noexcept -> bool;

	/// True when the calling thread owns any interpreter
	[[nodiscard]]
	static auto ownsAnyState() noexcept -> bool;

	/// The interpreter this thread acquired last, if it owns any
	[[nodiscard]]
	static auto lastOwnedIndex() noexcept -> std::optional<size_t>;

	/// Round robin interpreter assignment for a new script runtime
	[[nodiscard]]
	auto nextIndex() noexcept -> size_t;

	/**
	 * @brief The interpreter a script group runs on
	 *
	 * Every node of one prefab instance shares a group, so the calls between its scripts are plain re-entrant calls
	 * that never wait for another thread. Groups are numbered in the order they are created, which spreads them evenly
	 */
	[[nodiscard]]
	auto indexForGroup(uint64_t group) const noexcept -> size_t;

	/// Allocates the number of a new script group, never zero
	[[nodiscard]]
	static auto newGroup() noexcept -> uint64_t;

	[[nodiscard]]
	auto poolSize() const noexcept -> size_t {
		return m_pool_size;
	}

	/// The main thread of an interpreter; only the owner may touch it
	[[nodiscard]]
	auto mainState(size_t index) const noexcept -> lua_State*;

	/**
	 * @brief Runs `destroyer` while the interpreter `index` is owned
	 *
	 * Used to release anything that holds a reference inside the interpreter (registry references,
	 * userdata handles). Runs now when the interpreter is free or already owned by this thread,
	 * otherwise the work waits in the interpreter's queue and the next owner runs it
	 */
	void retire(size_t index, std::move_only_function<void()> destroyer) noexcept;

	/// Like retire(), but once the pool is gone the destroyer is leaked instead of run, the interpreters no longer exist
	static void retireOrLeak(size_t index, std::move_only_function<void()> destroyer) noexcept;

	/// How many times a thread that owned an interpreter found another one busy; non zero means work was queued
	[[nodiscard]]
	auto contentionCount() const noexcept -> uint64_t {
		return m_contention.load(std::memory_order_relaxed);
	}

	/// @brief Runs a chunk on state 0; intended for debug/console use
	auto runString(std::string_view lua_code) noexcept -> bool;

	/// Runs a chunk on the given interpreter, `error` receives the message of a failure
	auto runStringOn(size_t index, std::string_view lua_code, std::string* error = nullptr) noexcept -> bool;

	/// Re-registers the Node/Asset type-marker globals on every state
	void refreshTypeMarkers() noexcept;

private:
	static inline LuaState* instance = nullptr;

	struct Entry {
		lua_State* state = nullptr;
		std::recursive_mutex mutex;
		std::atomic<uint64_t> owner {0};                         ///< token of the owning thread, 0 while free
		int depth = 0;                                           ///< lock nesting, only touched by the owner
		std::mutex pending_mutex;
		std::vector<std::move_only_function<void()>> pending;    ///< work waiting for the next owner
		std::atomic<size_t> pending_count {0};
	};

	size_t m_pool_size = 0;
	std::vector<Entry> m_entries;
	std::atomic<size_t> m_next_index = 0;
	std::atomic<uint64_t> m_contention {0};

	LuaState();

	auto acquire(size_t index, bool wait) noexcept -> Lock;
	static void release(Entry& entry, size_t index) noexcept;
	static void drainPending(Entry& entry) noexcept;

	static void registerApi(lua_State* state) noexcept;
	static void registerTypeMarkers(lua_State* state) noexcept;
};

}

/// Debug check that the calling thread owns the interpreter `state` belongs to
#ifdef _DEBUG
#define TOAST_LUA_ASSERT_OWNED(state)                                                                                          \
	TOAST_ASSERT(                                                                                                                \
	    ::scripting::LuaState::ownedByCurrentThread(state), "Lua", "A Lua interpreter was used by a thread that does not own it" \
	)
#else
#define TOAST_LUA_ASSERT_OWNED(state) ((void)(state))
#endif
