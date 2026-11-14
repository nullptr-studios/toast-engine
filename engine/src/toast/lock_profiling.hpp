/**
 * @file lock_profiling.hpp
 * @author dario
 * @date 07/11/2026
 */

#pragma once

#include <condition_variable>
#include <mutex>
#include <shared_mutex>
#include <toast/build_config.hpp>

#if defined(TRACY_ENABLE) && defined(TOAST_PROFILE_LOCKS)
#include <tracy/Tracy.hpp>

#define TOAST_LOCKABLE(type, name, desc) TracyLockableN(type, name, desc)
#define TOAST_SHARED_LOCKABLE(type, name, desc) TracySharedLockableN(type, name, desc)
#define TOAST_LOCKABLE_BASE(type) LockableBase(type)
#define TOAST_SHARED_LOCKABLE_BASE(type) SharedLockableBase(type)

namespace toast {
/// Tracy lockables are not std::mutex so waits need the generic condition variable
using ConditionVariable = std::condition_variable_any;
}
#else
#define TOAST_LOCKABLE(type, name, desc) type name
#define TOAST_SHARED_LOCKABLE(type, name, desc) type name
#define TOAST_LOCKABLE_BASE(type) type
#define TOAST_SHARED_LOCKABLE_BASE(type) type

namespace toast {
using ConditionVariable = std::condition_variable;
}
#endif
