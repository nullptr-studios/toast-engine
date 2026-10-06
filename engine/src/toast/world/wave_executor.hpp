/**
 * @file wave_executor.hpp
 * @author Xein
 * @date 6 Oct 2026
 * @brief Runs a plan of jobs on the thread pool, one interpreter per job
 *
 * Whatever has to run Lua on one interpreter is one job, and a job's items run one after the other, so an interpreter is only
 * ever used by one worker at a time and nothing waits for another worker's interpreter. The tick waves, the construction of
 * scripts in a prefab and the init/begin of a tree all run through here
 */

#pragma once
#include <cstddef>
#include <memory>
#include <toast/export.hpp>
#include <type_traits>
#include <vector>

namespace toast::_detail {

struct WaveJob {
	std::vector<size_t> items;
	std::vector<size_t> interpreters;
};

class ItemRunner {
public:
	template<typename Callable>
	  requires(!std::is_same_v<std::remove_cvref_t<Callable>, ItemRunner>)
	ItemRunner(Callable& callable) noexcept    // NOLINT(google-explicit-constructor)
	    : m_object(const_cast<std::remove_const_t<Callable>*>(std::addressof(callable))),
	      m_invoke([](void* object, size_t item) { (*static_cast<Callable*>(object))(item); }) { }

	void operator()(size_t item) const { m_invoke(m_object, item); }

private:
	void* m_object;
	void (*m_invoke)(void*, size_t);
};

struct ExecuteOptions {
	bool non_blocking = true;
	const char* label = "Wave worker";
};

/**
 * @brief Runs the jobs of a plan in parallel and returns once every item has run
 * @param item_count How many items the plan has, every item in exactly one job
 */
TOAST_API void
    runJobs(const std::vector<WaveJob>& jobs, size_t item_count, ItemRunner run_item, const ExecuteOptions& options = {});

}
