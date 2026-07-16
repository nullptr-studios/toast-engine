/**
 * @file engine_defs.hpp
 * @author Dario
 * @date 06 Jul 2026
 *
 * @brief Engine-wide definitions and helper macros for cross-platform compatibility
 */

#pragma once

#ifdef __cplusplus
#if defined(_MSC_VER)
#define TOAST_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define TOAST_FORCE_INLINE __attribute__((always_inline))
#else
#define TOAST_FORCE_INLINE inline
#endif
#else
#define TOAST_FORCE_INLINE
#endif
