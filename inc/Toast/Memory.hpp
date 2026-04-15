/**
 * @file Memory.hpp
 * @author Xein
 * @date 15/03/25
 * @brief Memory utilities - uses default global allocator with optional Tracy profiling.
 */

#pragma once

#include <cstddef>

#ifdef TRACY_ENABLE
/// Disables Tracy profiling during shutdown to prevent crashes
extern "C" void DisableTracyProfiling() noexcept;
#endif

namespace toast::memory {

// No pool allocator - all allocations go through the default global operator new/delete.
// Tracy memory profiling is applied via the global operator overloads in Memory.cpp when TRACY_ENABLE is defined.

}    // namespace toast::memory
