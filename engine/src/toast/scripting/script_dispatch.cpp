#include "script_dispatch.hpp"

#include "lua_state.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <toast/thread_pool.hpp>
#include <toast/world/wave_executor.hpp>
#include <tracy/Tracy.hpp>
#include <unordered_map>
#include <utility>
#include <vector>

namespace scripting {

namespace {

struct DispatchJob {
	size_t vm_index = 0;
	std::move_only_function<void()> run;
};

// Interpreters past this are not tracked and never make a call queue behind others, the pool is far smaller
constexpr size_t k_tracked_interpreters = 1024;

// Fewer queued calls than this run one after the other on the thread that delivers, handing them out costs more than it saves
constexpr size_t k_min_parallel_batch = 8;

// Leaked on purpose: queued jobs hold node handles and Lua references that must not be destroyed during static teardown
auto& g_dispatch_mutex = *new std::mutex();                  // NOLINT
auto& g_dispatch_queue = *new std::vector<DispatchJob>();    // NOLINT
std::atomic<size_t> g_dispatch_pending {0};                  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
thread_local bool t_dispatch_delivering = false;             // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// Jobs per interpreter that were queued and have not finished. A job counts until it ran, so the ones of a batch that is
// being delivered still hold back the calls that are issued meanwhile
std::array<std::atomic<size_t>, k_tracked_interpreters> g_waiting_jobs {};    // NOLINT

void markQueued(size_t vm_index) noexcept {
	if (vm_index < k_tracked_interpreters) {
		g_waiting_jobs[vm_index].fetch_add(1, std::memory_order_release);
	}
}

void markFinished(size_t vm_index) noexcept {
	if (vm_index < k_tracked_interpreters) {
		g_waiting_jobs[vm_index].fetch_sub(1, std::memory_order_release);
	}
}

/// Takes a job off the waiting count once it ran, was skipped or was dropped
class FinishedJob {
public:
	explicit FinishedJob(size_t vm_index) noexcept : m_vm_index(vm_index) { }

	~FinishedJob() { markFinished(m_vm_index); }

	FinishedJob(const FinishedJob&) = delete;
	auto operator=(const FinishedJob&) -> FinishedJob& = delete;
	FinishedJob(FinishedJob&&) = delete;
	auto operator=(FinishedJob&&) -> FinishedJob& = delete;

private:
	size_t m_vm_index;
};
}

void ScriptDispatch::enqueue(size_t vm_index, std::move_only_function<void()> job) {
	if (!job) {
		return;
	}
	std::scoped_lock lock(g_dispatch_mutex);
	g_dispatch_queue.push_back(DispatchJob {.vm_index = vm_index, .run = std::move(job)});
	g_dispatch_pending.store(g_dispatch_queue.size(), std::memory_order_release);
	markQueued(vm_index);
}

auto ScriptDispatch::mustQueue(size_t vm_index) noexcept -> bool {
	if (vm_index >= k_tracked_interpreters || g_waiting_jobs[vm_index].load(std::memory_order_acquire) == 0) {
		return false;
	}
	// A thread that owns the interpreter itself is already inside it, and one that owns nothing waits for it
	return LuaState::exists() && LuaState::ownsAnyState() && !LuaState::ownedByCurrentThread(vm_index);
}

auto ScriptDispatch::pending() noexcept -> size_t {
	return g_dispatch_pending.load(std::memory_order_acquire);
}

auto ScriptDispatch::deliver(int max_rounds) -> size_t {
	if (g_dispatch_pending.load(std::memory_order_acquire) == 0 || t_dispatch_delivering || !LuaState::exists() ||
	    LuaState::ownsAnyState()) {
		return 0;
	}

	ZoneScopedN("Lua dispatch");    // NOLINT
	t_dispatch_delivering = true;
	size_t delivered = 0;
	for (int round = 0; round < max_rounds; ++round) {
		std::vector<DispatchJob> batch;
		{
			std::scoped_lock lock(g_dispatch_mutex);
			batch.swap(g_dispatch_queue);
			g_dispatch_pending.store(0, std::memory_order_release);
		}
		if (batch.empty()) {
			break;
		}

		// Runs one job on the interpreter it is for. Whoever runs it owns nothing, so lock() waits for the interpreter
		std::atomic<size_t> ran {0};
		auto run_job = [&batch, &ran](size_t index) {
			DispatchJob& job = batch[index];
			const FinishedJob finished(job.vm_index);
			LuaState::Lock guard = LuaState::get().lock(job.vm_index);
			if (!guard) {
				return;
			}
			job.run();
			ran.fetch_add(1, std::memory_order_relaxed);
		};

		// A batch that touches several interpreters is split by interpreter and run on the pool. The jobs of one interpreter
		// stay in the order they were queued, so they apply in the order they were issued, and the interpreters do not wait
		// for each other. A small batch is not worth handing out
		std::vector<toast::_detail::WaveJob> jobs;
		if (batch.size() >= k_min_parallel_batch && toast::ThreadPool::workerCount() > 1) {
			std::unordered_map<size_t, size_t> job_of_interpreter;
			for (size_t index = 0; index < batch.size(); ++index) {
				const auto [slot, inserted] = job_of_interpreter.try_emplace(batch[index].vm_index, jobs.size());
				if (inserted) {
					jobs.push_back(toast::_detail::WaveJob {.items = {}, .interpreters = {batch[index].vm_index}});
				}
				jobs[slot->second].items.push_back(index);
			}
		}
		if (jobs.size() > 1) {
			std::ranges::stable_sort(jobs, [](const toast::_detail::WaveJob& lhs, const toast::_detail::WaveJob& rhs) {
				return lhs.items.size() > rhs.items.size();
			});
			toast::_detail::runJobs(jobs, batch.size(), run_job, {.non_blocking = false, .label = "Lua dispatch worker"});
		} else {
			for (size_t index = 0; index < batch.size(); ++index) {
				run_job(index);
			}
		}
		delivered += ran.load(std::memory_order_relaxed);
	}
	t_dispatch_delivering = false;
	return delivered;
}

void ScriptDispatch::clear() noexcept {
	std::vector<DispatchJob> dropped;
	{
		std::scoped_lock lock(g_dispatch_mutex);
		dropped.swap(g_dispatch_queue);
		g_dispatch_pending.store(0, std::memory_order_release);
	}
	for (const DispatchJob& job : dropped) {
		markFinished(job.vm_index);
	}
}

}
