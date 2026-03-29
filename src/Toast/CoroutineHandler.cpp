#include "Toast/CoroutineHandler.hpp"

#include "Toast/SimulateWorldEvent.hpp"

#include <Toast/Log.hpp>
#include <Toast/Profiler.hpp>

namespace toast {

CoroutineHandler* CoroutineHandler::instance = nullptr;

CoroutineHandler::CoroutineHandler() {
	instance = this;

#ifdef TOAST_EDITOR
	listener.Subscribe<SimulateWorldEvent>([this](SimulateWorldEvent* e) {
		if (not e->value) {    // If Not Simulating
			pendingTasks.clear();
		}
		return false;
	});
#endif
}

void CoroutineHandler::AddTask(CoroutineInfo&& info) {
	if (not instance) {
		TOAST_ERROR("Trying to add a coroutine but Handler doesn't exist");
		return;
	}

	instance->pendingTasks.emplace_back(info);
}

void CoroutineHandler::Tick() {
	PROFILE_ZONE;

	auto now = std::chrono::steady_clock::now();

	// Find tasks that are finished
	auto boundary = std::partition(pendingTasks.begin(), pendingTasks.end(), [now](const CoroutineInfo& info) {
		return now < info.wake_up;    // Move to the front unfinished tasks
	});

	// Temp list with resume tasks
	std::vector<CoroutineInfo> toResume;
	std::move(boundary, pendingTasks.end(), std::back_inserter(toResume));
	pendingTasks.erase(boundary, pendingTasks.end());

	// Execute coroutines
	for (auto& task : toResume) {
		if (task.handle && !task.handle.done()) {
			task.handle.resume();
		}
	}
}

}
