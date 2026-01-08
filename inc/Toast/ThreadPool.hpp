/// @file ThreadPool.hpp
/// @author Xein
/// @date 6 Nov 2025

#pragma once
#include <condition_variable>

namespace toast {

class ThreadPool {
public:
	/// @brief Initializes the thread pool
	/// @param size Number of workers to create
	void Init(size_t size);

	/// @brief Adds a job to the queue to be picked by a worker
	void QueueJob(std::function<void()>&& job);

	/// @brief Ends the thread pool
	void Destroy();

	[[nodiscard]]
	bool busy();    ///< @brief Checks if there are workers still performing a job

private:
	void ThreadLoop();

	bool m_shouldStop = false;
	std::mutex m_queueMutex;
	std::condition_variable m_conditionMutex;
	std::vector<std::thread> m_workers;
	std::queue<std::function<void()>> m_jobs;
};

}
