/**
 * @file ThreadPool.hpp
 * @author Xein
 * @date 6 Nov 2025
 * @brief Thread pool implementation for parallel task execution.
 *
 * Provides a simple thread pool for executing tasks asynchronously.
 * Used internally for scene loading and other background operations.
 */

#pragma once
#include <condition_variable>

namespace toast {

/**
 * @class ThreadPool
 * @brief A simple thread pool for executing jobs asynchronously.
 *
 * The ThreadPool manages a fixed number of worker threads that process
 * jobs from a queue. Jobs are executed in FIFO order, though completion
 * order is not guaranteed due to parallel execution.
 *
 * @par Usage Example:
 * @code
 * ThreadPool pool;
 * pool.Init(4);  // Create 4 worker threads
 *
 * pool.QueueJob([]() {
 *     // Do some heavy work...
 * });
 *
 * // Wait for all jobs to complete
 * while (pool.busy()) {
 *     std::this_thread::sleep_for(std::chrono::milliseconds(10));
 * }
 *
 * pool.Destroy();
 * @endcode
 *
 * @note The pool should be destroyed before the program exits to ensure
 *       all worker threads are properly joined.
 *
 * @see World (uses ThreadPool for async scene loading)
 */
class ThreadPool {
public:
	/**
	 * @brief Initializes the thread pool with worker threads.
	 *
	 * Creates the specified number of worker threads. If size is 0
	 * or greater than hardware concurrency, uses hardware_concurrency().
	 *
	 * @param size Number of worker threads to create.
	 *
	 * @note This method should only be called once per ThreadPool instance.
	 */
	void Init(size_t size);

	/**
	 * @brief Queues a job for execution by a worker thread.
	 *
	 * The job will be executed as soon as a worker thread becomes available.
	 * Jobs are processed in FIFO order.
	 *
	 * @param job The function to execute (moved into the queue).
	 *
	 * @par Example:
	 * @code
	 * pool.QueueJob([data = std::move(myData)]() {
	 *     processData(data);
	 * });
	 * @endcode
	 */
	void QueueJob(std::function<void()>&& job);

	/**
	 * @brief Destroys the thread pool and waits for all workers to finish.
	 *
	 * Signals all worker threads to stop and joins them. Any jobs still
	 * in the queue will NOT be executed.
	 *
	 * @warning This method blocks until all worker threads have terminated.
	 */
	void Destroy();

	/**
	 * @brief Checks if there are pending jobs in the queue.
	 * @return true if jobs are waiting to be processed, false if queue is empty.
	 *
	 * @note This does not indicate if workers are currently executing jobs,
	 *       only if there are jobs waiting in the queue.
	 */
	[[nodiscard]]
	bool busy();

private:
	/**
	 * @brief Main loop executed by each worker thread.
	 *
	 * Workers wait for jobs to become available, execute them,
	 * then return to waiting. Exits when m_shouldStop is true.
	 */
	void ThreadLoop();

	/// @brief Flag to signal workers to stop.
	bool m_shouldStop = false;

	/// @brief Mutex protecting the job queue.
	std::mutex m_queueMutex;

	/// @brief Condition variable for worker notification.
	std::condition_variable m_conditionMutex;

	/// @brief Vector of worker threads.
	std::vector<std::thread> m_workers;

	/// @brief Queue of pending jobs.
	std::queue<std::function<void()>> m_jobs;
};

}
