#include "thread_pool.hpp"

#include "log.hpp"

#include <format>
#include <tracy/Tracy.hpp>

namespace toast {

ThreadPool::ThreadPool(size_t size) {
	ZoneScoped;

	// safety check, we don't want more threads than available
	const size_t max_thread_num = std::thread::hardware_concurrency();
	if (size == 0) {
		size = max_thread_num;
	}

	size_t target_thread_num = std::min(size, max_thread_num);
	for (size_t i = 0; i < target_thread_num; ++i) {
		m.workers.emplace_back(&ThreadPool::threadLoop, this, i);
	}
	TOAST_INFO("ThreadPool", "Created thread pool with {0} workers", target_thread_num);
}

void ThreadPool::queueJob(std::function<void()>&& job) {
	ZoneScoped;
	auto& o = get();

	{
		std::unique_lock<std::mutex> lock(o.m.queue_mutex);
		o.m.jobs.emplace(std::move(job));
	}

	o.m.job_available.notify_one();
}

void ThreadPool::destroy() {
	ZoneScoped;

	{
		std::unique_lock<std::mutex> lock(m.queue_mutex);
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
	ZoneScoped;
	std::unique_lock<std::mutex> lock(m.queue_mutex);
	return !m.jobs.empty() || m.active_jobs > 0;
}

void ThreadPool::waitIdle() {
	ZoneScoped;
	std::unique_lock<std::mutex> lock(m.queue_mutex);
	m.all_done.wait(lock, [this] { return m.jobs.empty() && m.active_jobs == 0; });
}

void ThreadPool::threadLoop(size_t id) {
	tracy::SetThreadName(std::format("Worker #{}", id).c_str());

	while (true) {
		std::function<void()> job;

		{
			std::unique_lock<std::mutex> lock(m.queue_mutex);
			m.job_available.wait(lock, [this] { return !m.jobs.empty() || m.should_stop; });
			if (m.should_stop) {
				return;
			}
			// move out of queue rather than copy
			job = std::move(m.jobs.front());
			m.jobs.pop();
			++m.active_jobs;
		}

		{
			ZoneScopedN("thread::job()");
			ZoneNameF("thread_%i::job()", (int)id);
			job();
		}

		// Decrement and notify waitIdle() if pool is now fully idle
		if (--m.active_jobs == 0) {
			std::unique_lock<std::mutex> lock(m.queue_mutex);
			if (m.jobs.empty()) {
				m.all_done.notify_all();
			}
		}
	}
}

auto ThreadPool::create() noexcept -> std::unique_ptr<ThreadPool> {
	assert(not instance && "ThreadPool already exists");
	instance = new ThreadPool(thread_count);
	return std::unique_ptr<ThreadPool>(instance);
}

auto ThreadPool::get() noexcept -> ThreadPool& {
	assert(instance && "ThreadPool doesn't exist");
	return *instance;
}
}    // namespace toast
