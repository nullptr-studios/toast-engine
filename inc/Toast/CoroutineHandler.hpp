/// @file CoroutineHandler.hpp
/// @author Xein
/// @date 27 Feb 2026

#pragma once
#include <chrono>
#include <coroutine>
#include <vector>

namespace toast {

struct CoroutineTask {
	struct promise_type {
		CoroutineTask get_return_object() {
			return {};
		}

		std::suspend_never initial_suspend() noexcept {
			return {};
		}

		std::suspend_never final_suspend() noexcept {
			return {};
		}

		void return_void() { }

		void unhandled_exception() {
			throw;
			// i love this comment -dante
		}
	};
};

struct CoroutineInfo {
	std::chrono::steady_clock::time_point wake_up;
	std::coroutine_handle<> handle;
};

class CoroutineHandler {
public:
	CoroutineHandler();
	static void AddTask(CoroutineInfo&& info);

	void Tick();

private:
	static CoroutineHandler* instance;
	std::vector<CoroutineInfo> pendingTasks;
};

}
