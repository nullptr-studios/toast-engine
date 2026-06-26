/// @file export.h
/// @author Xein
/// @date 17 Feb 2026
/// This just contains an small header guard for dll exports on the C API

#pragma once

#if defined(_WIN32)
#if defined(TOAST_EXPORT)
#define TOAST_C_API __declspec(dllexport)
#else
#define TOAST_C_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#define TOAST_C_API __attribute__((visibility("default")))
#else
#define TOAST_C_API
#endif

#if defined(__cplusplus)
#define NOEXCEPT noexcept
#else
#define NOEXCEPT
#endif
