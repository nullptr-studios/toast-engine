/// @file WaitAsync.hpp
/// @author Xein
/// @date 27 Feb 2026

#pragma once

#include "CoroutineHandler.hpp"

#include <chrono>

namespace toast {

struct WaitSeconds {
	std::chrono::steady_clock::time_point target_time;

	WaitSeconds(float seconds) {
		auto dur = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(seconds));
		target_time = std::chrono::steady_clock::now() + dur;
	}

	bool await_ready() const {
		return std::chrono::steady_clock::now() >= target_time;
	}

	void await_suspend(std::coroutine_handle<> h) {
		CoroutineHandler::AddTask({ target_time, h });
	}

	void await_resume() { }
};

}
