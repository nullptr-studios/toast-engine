/**
 * @file thread_pool.hpp
 * @author Xein
 * @date 6 Nov 2025
 * @brief Thread pool implementation for parallel task execution
 */
#pragma once
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace toast {

/**
 * @class ThreadPool
 * @brief A simple thread pool for executing jobs asynchronously
 *
 * The ThreadPool manages a fixed number of worker threads that process
 * jobs from a queue. Jobs are executed in FIFO order, though completion
 * order is not guaranteed due to parallel execution.
 *
 * @par Usage Example:
 * @code
 * ThreadPool pool = ThreadPool::create(); // Create pool with THREAD_COUNT workers
 *
 * pool.queueJob([]() {
 *     // Do some heavy work...
 * });
 *
 * pool.waitIdle(); // Block until all jobs are done
 * pool.destroy();
 * @endcode
 *
 * @note The pool should be destroyed before the program exits to ensure
 *       all worker threads are properly joined.
 *
 * @see World (uses ThreadPool for async scene loading)
 */
class ThreadPool {
public:
	static constexpr size_t thread_count = 6;    ///< Number of workers on the pool

	/**
	 * @brief Initializes the pool and returns a pointer with ownership
	 *
	 * Remember to terminate the pool properly before it gets discarded:
	 * @code
	 * pool.waitIdle();
	 * pool.destroy();
	 * @endcode
	 */
	static auto create() noexcept -> std::unique_ptr<ThreadPool>;

	/**
	 * @brief Queues a job for execution by a worker thread
	 *
	 * The job will be executed as soon as a worker thread becomes available.
	 * Jobs are processed in FIFO order. The thread pool uses move_only funcions.
	 *
	 * @param job The function to execute (moved into the queue)
	 *
	 * @par Example:
	 * @code
	 * pool.queueJob([data = std::move(myData)]() {
	 *     processData(data);
	 * });
	 * @endcode
	 */
	template<typename T>
	static auto push(T&& job) -> std::future<std::invoke_result_t<T>>;

	/**
	 * @brief Destroys the thread pool and waits for all workers to finish.
	 *
	 * Signals all worker threads to stop and joins them. Any jobs still
	 * in the queue will NOT be executed.
	 *
	 * @warning This method blocks until all worker threads have terminated.
	 */
	void destroy();

	/**
	 * @brief Checks if the pool has pending or in-flight jobs.
	 *
	 * Returns true if there are jobs in the queue OR if any worker
	 * is currently executing a job.
	 *
	 * @return true if any work is pending or in progress.
	 */
	[[nodiscard]] [[deprecated("Use waitIdle() instead")]]
	auto busy() -> bool;

	/**
	 * @brief Blocks the calling thread until all queued and active jobs complete.
	 *
	 * More efficient than spinning on busy() — uses a condition variable
	 * to sleep until the pool drains completely.
	 */
	void waitIdle();

	// No copy and move constructors
	ThreadPool(ThreadPool&) = delete;
	ThreadPool(ThreadPool&&) = delete;
	auto operator=(const ThreadPool&) -> ThreadPool& = delete;
	auto operator=(ThreadPool&&) -> ThreadPool& = delete;

	~ThreadPool() {
		waitIdle();
		destroy();
	}

private:
	ThreadPool(size_t size);
	inline static ThreadPool* instance = nullptr;
	static auto get() noexcept -> ThreadPool&;

	void threadLoop();
	static void enqueue(std::move_only_function<void()>&& job);

	struct {
		bool should_stop = false;                            ///< Flag to signal workers to stop
		std::atomic<int> active_jobs = 0;                    ///< Number of jobs currently executing
		std::mutex queue_mutex;                              ///< Mutex protecting the job queue
		std::condition_variable job_available;               ///< Notified when a job is enqueued or stop is requested
		std::condition_variable all_done;                    ///< Notified when activeJobs hits 0 and queue is empty
		std::vector<std::jthread> workers;                   ///< Worker threads (auto-join on destruction)
		std::queue<std::move_only_function<void()>> jobs;    ///< Pending job queue
	} m;
};

template<typename T>
auto ThreadPool::push(T&& job) -> std::future<std::invoke_result_t<T>> {
	using result_t = std::invoke_result_t<T>;

	std::packaged_task<result_t()> task(std::forward<T>(job));
	auto future = task.get_future();
	enqueue([task = std::move(task)]() mutable { task(); });
	return future;
}

}
