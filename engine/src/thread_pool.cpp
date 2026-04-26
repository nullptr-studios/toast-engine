#include "thread_pool.hpp"

namespace toast {

ThreadPool::ThreadPool(size_t size) {
	// safety check, we don't want more threads than available
	const size_t max_thread_num = std::thread::hardware_concurrency();
	if (size == 0) {
		size = max_thread_num;
	}

	size_t target_thread_num = std::min(size, max_thread_num);
	for (size_t i = 0; i < target_thread_num; ++i) {
		m.workers.emplace_back(&ThreadPool::threadLoop, this);
	}
	// TODO: TOAST_TRACE("Created thread pool with {0} workers", target_thread_num);
}

void ThreadPool::queueJob(std::function<void()>&& job) {
	auto& o = get();

	{
		std::unique_lock<std::mutex> lock(o.m.queueMutex);
		o.m.jobs.emplace(std::move(job));
	}

	o.m.jobAvailable.notify_one();
}

void ThreadPool::destroy() {
	{
		std::unique_lock<std::mutex> lock(m.queueMutex);
		m.shouldStop = true;
	}

	m.jobAvailable.notify_all();
	m.workers.clear();
	// TODO: TOAST_TRACE("Destroyed thread pool");
}

bool ThreadPool::busy() {
	std::unique_lock<std::mutex> lock(m.queueMutex);
	return !m.jobs.empty() || m.activeJobs > 0;
}

void ThreadPool::waitIdle() {
	std::unique_lock<std::mutex> lock(m.queueMutex);
	m.allDone.wait(lock, [this] { return m.jobs.empty() && m.activeJobs == 0; });
}

void ThreadPool::threadLoop() {
	while (true) {
		std::function<void()> job;

		{
			std::unique_lock<std::mutex> lock(m.queueMutex);
			m.jobAvailable.wait(lock, [this] { return !m.jobs.empty() || m.shouldStop; });
			if (m.shouldStop) {
				return;
			}
			// move out of queue rather than copy
			job = std::move(m.jobs.front());
			m.jobs.pop();
			++m.activeJobs;
		}

		job();

		// Decrement and notify waitIdle() if pool is now fully idle
		if (--m.activeJobs == 0) {
			std::unique_lock<std::mutex> lock(m.queueMutex);
			if (m.jobs.empty()) {
				m.allDone.notify_all();
			}
		}
	}
}

auto ThreadPool::create() noexcept -> std::unique_ptr<ThreadPool> {
	assert(not instance && "ThreadPool already exists");
	instance = new ThreadPool(THREAD_COUNT);
	return std::unique_ptr<ThreadPool>(instance);
}

auto ThreadPool::get() noexcept -> ThreadPool& {
	assert(instance && "ThreadPool doesn't exist");
	return *instance;
}
}    // namespace toast
