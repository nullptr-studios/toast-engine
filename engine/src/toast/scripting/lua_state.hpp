/**
 * @file lua_state.hpp
 * @author Xein
 * @date 10 Jul 2026
 *
 * @brief Pool of independent Lua interpreters
 *
 * A Lua interpreter is not thread safe, so every interpreter has exactly one owner thread at a time
 * and only the owner may touch its stack, its registry or any reference that lives inside it
 *
 * The rules that keep this deadlock free:
 *  - a thread waits for an interpreter only while it owns no other interpreter
 *  - a thread that owns one and finds another one busy gets an empty Lock and must hand the work
 *    to ScriptDispatch instead of waiting
 *  - a worker of a tick wave never waits for an interpreter at all, see NonBlockingScope: the scheduler gives every
 *    interpreter to one job of the wave, so a busy one means something outside the wave has it
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
#include <unordered_map>
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
	 * an interpreter never waits for a second one
	 */
	[[nodiscard]]
	auto lock(size_t index) noexcept -> Lock;

	/// Non-blocking variant of lock()
	[[nodiscard]]
	auto tryLock(size_t index) noexcept -> Lock;

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

	/// How many runtimes of one script group share an interpreter before the next ones go to another
	static constexpr uint32_t k_group_chunk = 8;

	/**
	 * @brief Picks the interpreter for a new script runtime
	 *
	 * The runtimes of one script group (the nodes of a prefab instance) are kept together, so the calls between their
	 * scripts are plain calls that nothing else can be running at the same time. No interpreter hosts more than
	 * k_group_chunk of one group though: a group with hundreds of scripts is spread over several, or those scripts could
	 * only ever tick one after the other. Runtimes without a group, and every new chunk of a group, go to the least
	 * loaded interpreter that has room for them
	 *
	 * @param forced Place the runtime there instead, for a thread that owns that interpreter and cannot wait for another
	 * @note Every call has to be paired with unassign() once the runtime is gone
	 */
	[[nodiscard]]
	auto assign(uint64_t group, std::optional<size_t> forced = std::nullopt) -> size_t;

	/// Gives back what assign() handed out
	void unassign(uint64_t group, size_t index) noexcept;

	/// How many runtimes every interpreter hosts, for diagnostics and tests
	[[nodiscard]]
	auto loads() const -> std::vector<uint32_t>;

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
	 * @brief Runs destroyer while the interpreter index is owned
	 *
	 * Used to release anything that holds a reference inside the interpreter
	 */
	void retire(size_t index, std::move_only_function<void()> destroyer) noexcept;

	/// Like retire(), but once the pool is gone the destroyer is leaked instead of run, the interpreters no longer exist
	static void retireOrLeak(size_t index, std::move_only_function<void()> destroyer) noexcept;

	/// How many times a thread that owned an interpreter found another one busy; non zero means work was queued
	[[nodiscard]]
	auto contentionCount() const noexcept -> uint64_t {
		return m_contention.load(std::memory_order_relaxed);
	}

	/// How many times a thread had to wait for an interpreter that another thread owned
	[[nodiscard]]
	auto waitCount() const noexcept -> uint64_t {
		return m_waits.load(std::memory_order_relaxed);
	}

	/**
	 * @brief While one exists on a thread, lock() on that thread never waits
	 */
	class TOAST_API NonBlockingScope {
	public:
		NonBlockingScope() noexcept;
		~NonBlockingScope();

		NonBlockingScope(const NonBlockingScope&) = delete;
		auto operator=(const NonBlockingScope&) -> NonBlockingScope& = delete;
		NonBlockingScope(NonBlockingScope&&) = delete;
		auto operator=(NonBlockingScope&&) -> NonBlockingScope& = delete;
	};

	/// @brief Runs a chunk on state 0; intended for debug/console use
	auto runString(std::string_view lua_code) noexcept -> bool;

	/// Runs a chunk on the given interpreter
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
	std::atomic<uint64_t> m_contention {0};
	std::atomic<uint64_t> m_waits {0};

	/// Where the runtimes of a script group are: how many on each interpreter
	struct GroupPlacement {
		std::vector<uint32_t> per_index;
		size_t index = 0;
		uint32_t members = 0;
	};

	// Guards the placement bookkeeping, which only changes while runtimes are created and destroyed
	mutable std::mutex m_placement_mutex;
	std::vector<uint32_t> m_loads;    ///< runtimes per interpreter
	std::unordered_map<uint64_t, GroupPlacement> m_groups;
	size_t m_next_tie = 0;

	LuaState();

	auto acquire(size_t index, bool wait) noexcept -> Lock;
	static void release(Entry& entry, size_t index) noexcept;
	static void drainPending(Entry& entry) noexcept;

	static void registerApi(lua_State* state) noexcept;
	static void registerTypeMarkers(lua_State* state) noexcept;
};

}

/// Check that the calling thread owns the interpreter `state` belongs to. Debug builds assert it, and the builds that
/// define TOAST_LUA_OWNERSHIP_CHECKS (RelWithDebInfo) abort with a message, since a violation is a corrupted heap
/// waiting to happen and the abort points at where it started
#ifdef _DEBUG
#define TOAST_LUA_ASSERT_OWNED(state)                                                                                          \
	TOAST_ASSERT(                                                                                                                \
	    ::scripting::LuaState::ownedByCurrentThread(state), "Lua", "A Lua interpreter was used by a thread that does not own it" \
	)
#elif defined(TOAST_LUA_OWNERSHIP_CHECKS)
#define TOAST_LUA_ASSERT_OWNED(state)                                                       \
	do {                                                                                      \
		if (!::scripting::LuaState::ownedByCurrentThread(state)) {                              \
			TOAST_CRITICAL("Lua", "A Lua interpreter was used by a thread that does not own it"); \
		}                                                                                       \
	} while (0)
#else
#define TOAST_LUA_ASSERT_OWNED(state) ((void)(state))
#endif
