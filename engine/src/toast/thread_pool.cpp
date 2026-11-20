#include "thread_pool.hpp"

#include <tracy/Tracy.hpp>

namespace toast {

ThreadPool::ThreadPool() {
	// Leave 4 threads for the OS
	size_t hardware_thread_count = std::max<size_t>(std::thread::hardware_concurrency(), 1);
	size_t target_thread_num = hardware_thread_count > 4 ? hardware_thread_count - 4 : 1;
	for (size_t i = 0; i < target_thread_num; ++i) {
		m.workers.emplace_back(&ThreadPool::threadLoop, this);
	}
	TOAST_INFO("ThreadPool", "Created thread pool with {0} workers", target_thread_num);
}

void ThreadPool::pushRaw(std::move_only_function<void()>&& job) {
	enqueue(std::move(job), false);
}

void ThreadPool::lpPushRaw(std::move_only_function<void()>&& job) {
	enqueue(std::move(job), true);
}

auto ThreadPool::lowPriorityRunnable() const -> bool {
	const int limit = static_cast<int>(std::max<size_t>(m.workers.size() / 2, 1));
	return !m.low_jobs.empty() && m.active_low_jobs < limit;
}

void ThreadPool::enqueue(std::move_only_function<void()>&& job, bool low_priority) {
	auto& o = get();

	{
		std::unique_lock lock(o.m.queue_mutex);
		(low_priority ? o.m.low_jobs : o.m.jobs).emplace(std::move(job));
	}

	o.m.job_available.notify_one();
}

void ThreadPool::destroy() {
	{
		std::unique_lock lock(m.queue_mutex);
		m.should_stop = true;
	}

	m.job_available.notify_all();
	for (auto& worker : m.workers) {
		if (worker.joinable()) {
			worker.join();
		}
	}

	m.workers.clear();
	TOAST_INFO("ThreadPool", "Destroyed thread pool");
}

auto ThreadPool::busy() -> bool {
	std::unique_lock lock(m.queue_mutex);
	return !m.jobs.empty() || !m.low_jobs.empty() || m.active_jobs > 0;
}

void ThreadPool::waitIdle() {
	std::unique_lock lock(m.queue_mutex);
	m.all_done.wait(lock, [this] { return m.jobs.empty() && m.low_jobs.empty() && m.active_jobs == 0; });
}

namespace {
thread_local bool t_thread_pool_worker = false;
}

auto ThreadPool::onWorkerThread() noexcept -> bool {
	return t_thread_pool_worker;
}

auto ThreadPool::workerCount() -> size_t {
	return instance ? instance->m.workers.size() : 0;
}

auto ThreadPool::idleWorkers() noexcept -> size_t {
	return instance ? static_cast<size_t>(std::max(0, instance->m.idle_workers.load(std::memory_order_acquire))) : 0;
}

void ThreadPool::threadLoop() {
	t_thread_pool_worker = true;
	static std::atomic<int> worker_id = 0;
	thread_local static std::string name = std::format("ThreadPool::worker-{}", worker_id++);
#ifdef TRACY_ENABLE
	tracy::SetThreadName(name.c_str());
#endif

	while (true) {
		std::move_only_function<void()> job;
		bool is_low = false;

		{
			std::unique_lock lock(m.queue_mutex);
			// track idle workers
			m.idle_workers.fetch_add(1, std::memory_order_release);
			m.job_available.wait(lock, [this] { return !m.jobs.empty() || lowPriorityRunnable() || m.should_stop; });
			m.idle_workers.fetch_sub(1, std::memory_order_release);
			if (m.should_stop) {
				return;
			}
			// move out of queue rather than copy; normal jobs take precedence
			if (!m.jobs.empty()) {
				job = std::move(m.jobs.front());
				m.jobs.pop();
			} else {
				job = std::move(m.low_jobs.front());
				m.low_jobs.pop();
				is_low = true;
				++m.active_low_jobs;
			}
			int active = ++m.active_jobs;
			TracyPlot("ThreadPool::active threads", static_cast<int64_t>(active));
		}

		{
			ZoneScopedN("ThreadPool::job()");
			job();
		}

		// Bookkeeping under the lock so a freed low-priority slot is never missed by sleeping workers
		{
			std::unique_lock lock(m.queue_mutex);
			int active = --m.active_jobs;
			TracyPlot("ThreadPool::active threads", static_cast<int64_t>(active));
			if (is_low) {
				--m.active_low_jobs;
				if (!m.low_jobs.empty()) {
					m.job_available.notify_one();
				}
			}
			if (active == 0 && m.jobs.empty() && m.low_jobs.empty()) {
				m.all_done.notify_all();
			}
		}
	}
}

auto ThreadPool::create() noexcept -> std::unique_ptr<ThreadPool> {
	assert(not instance && "ThreadPool already exists");
	instance = new ThreadPool;
	return std::unique_ptr<ThreadPool>(instance);
}

auto ThreadPool::get() noexcept -> ThreadPool& {
	assert(instance && "ThreadPool doesn't exist");
	return *instance;
}
}    // namespace toast
