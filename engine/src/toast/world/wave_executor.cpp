#include "wave_executor.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#endif
#include <thread>
#include <toast/scripting/lua_state.hpp>
#include <toast/thread_pool.hpp>
#include <tracy/Tracy.hpp>

namespace toast::_detail {

namespace {

inline void cpuRelax() noexcept {
#if defined(_M_X64) || defined(__x86_64__)
	_mm_pause();
#else
	std::this_thread::yield();
#endif
}

std::atomic<size_t> g_queued_helpers {0};    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
constexpr size_t k_queued_helpers_per_worker = 4;
constexpr auto k_grace_with_idle_workers = std::chrono::milliseconds(2);
constexpr auto k_grace_without_idle_workers = std::chrono::microseconds(150);

struct alignas(64) Claim {
	std::atomic<size_t> next {0};
	std::atomic<uint8_t> held {0};
};

struct Run {
	Run(size_t scripted_jobs, size_t items) : claims(scripted_jobs), next_plain(scripted_jobs), remaining(items) { }

	std::vector<Claim> claims;
	std::atomic<size_t> next_plain;
	std::atomic<size_t> remaining;
	std::atomic<size_t> starts {0};
	std::atomic<size_t> entered {0};
	std::atomic<int> inside {0};
};

class Inside {
public:
	explicit Inside(Run& run) noexcept : m_run(run) { m_run.inside.fetch_add(1); }

	~Inside() {
		if (m_run.inside.fetch_sub(1) == 1) {
			m_run.inside.notify_all();
		}
	}

	Inside(const Inside&) = delete;
	auto operator=(const Inside&) -> Inside& = delete;
	Inside(Inside&&) = delete;
	auto operator=(Inside&&) -> Inside& = delete;

private:
	Run& m_run;
};

/// Counts an item as done
void finishItem(Run& run) noexcept {
	if (run.remaining.fetch_sub(1) == 1) {
		run.remaining.notify_all();
	}
}
}

void runJobs(const std::vector<WaveJob>& jobs, size_t item_count, ItemRunner run_item, const ExecuteOptions& options) {
	if (jobs.empty() || item_count == 0) {
		return;
	}

	// Workers claim an interpreter that nobody is running, run one item of it and let go of it, so an interpreter is only
	// ever used by one worker at a time
	size_t scripted = 0;
	while (scripted < jobs.size() && !jobs[scripted].interpreters.empty()) {
		++scripted;
	}
	const auto run = std::make_shared<Run>(scripted, item_count);

	auto work = [run, &jobs, run_item, scripted, non_blocking = options.non_blocking, label = options.label] {
		Run& state = *run;
		const Inside inside(state);
		state.entered.fetch_add(1, std::memory_order_relaxed);
		if (state.remaining.load() == 0) {
			return;
		}

		// A tick wave never waits for an interpreter. If something outside the plan has one anyway, the call is queued for
		// the end of the wave instead of stalling this worker
		std::optional<scripting::LuaState::NonBlockingScope> no_waiting;
		if (non_blocking) {
			no_waiting.emplace();
		}
		ZoneScoped;    // NOLINT
		ZoneNameF("%s", label);

		size_t first = state.starts.fetch_add(1, std::memory_order_relaxed);
		unsigned failed_takes = 0;
		while (true) {
			bool worked = false;
			for (size_t step = 0; step < scripted && !worked; ++step) {
				const size_t index = (first + step) % scripted;
				const WaveJob& job = jobs[index];
				Claim& claim = state.claims[index];
				if (claim.next.load(std::memory_order_relaxed) >= job.items.size() ||
				    claim.held.exchange(1, std::memory_order_acquire) != 0) {
					continue;
				}
				const size_t item = claim.next.load(std::memory_order_relaxed);
				if (item < job.items.size()) {
					run_item(job.items[item]);
					claim.next.store(item + 1, std::memory_order_relaxed);
					finishItem(state);
					worked = true;
					// The next look starts at the following interpreter
					first = index + 1;
				}
				claim.held.store(0, std::memory_order_release);
			}
			if (!worked) {
				const size_t index = state.next_plain.fetch_add(1, std::memory_order_relaxed);
				if (index < jobs.size()) {
					run_item(jobs[index].items.front());
					finishItem(state);
					worked = true;
				}
			}

			if (worked) {
				failed_takes = 0;
				continue;
			}
			if (state.remaining.load() == 0) {
				return;
			}
			// Every interpreter that is left is being run by another worker
			// Pausing instead of spinning at full speed leaves the execution units to the hyperthread
			for (int spin = 0; spin < 32; ++spin) {
				cpuRelax();
			}
			if ((++failed_takes & 7) == 0) {
				std::this_thread::yield();
			}
		}
	};

	// More workers than jobs would only wait for the others
	const size_t workers = ThreadPool::workerCount();
	const size_t idle = ThreadPool::idleWorkers();
	const size_t helpers = std::min(jobs.size(), workers);
	const size_t queue_limit = workers * k_queued_helpers_per_worker;
	size_t queued = 0;
	for (; queued < helpers; ++queued) {
		if (g_queued_helpers.fetch_add(1) >= queue_limit) {
			g_queued_helpers.fetch_sub(1);
			break;
		}
		ThreadPool::pushRaw([work] {
			g_queued_helpers.fetch_sub(1);
			work();
		});
	}

	{
		ZoneScopedN("Wait for workers");    // NOLINT

		// The pool does the work, the thread that asked for it only waits
		// A thread that is a worker itself has nobody to wait for, and neither does one that found no pool to give the work to
		if (queued == 0 || ThreadPool::onWorkerThread()) {
			work();
		} else {
			// Workers that are waiting for a task show up at once
			const auto deadline =
			    std::chrono::steady_clock::now() + (idle != 0 ? k_grace_with_idle_workers : k_grace_without_idle_workers);
			unsigned spins = 0;
			while (run->remaining.load() != 0 && run->entered.load(std::memory_order_relaxed) == 0) {
				if (std::chrono::steady_clock::now() >= deadline) {
					work();
					break;
				}
				cpuRelax();
				if ((++spins & 63) == 0) {
					std::this_thread::yield();
				}
			}
		}

		for (size_t remaining = run->remaining.load(); remaining != 0; remaining = run->remaining.load()) {
			run->remaining.wait(remaining);
		}

		// Whoever is still in the middle of leaving
		for (int inside = run->inside.load(); inside != 0; inside = run->inside.load()) {
			run->inside.wait(inside);
		}
	}
}

}
