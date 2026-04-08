/**
 * @file export.hpp
 * @author Xein
 * @date 05 Apr 26
 */

#pragma once

#if defined(_WIN32)
	#if defined(TOAST_EXPORT)
		#define TOAST_API __declspec(dllexport)
	#else
		#define TOAST_API __declspec(dllimport)
	#endif
#elif defined(__GNUC__)
	#define TOAST_API __attribute__((visibility("default")))
#else
	#define TOAST_API
#endif

