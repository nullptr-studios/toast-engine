/// @file   Profiler.hpp
/// @author Xein
/// @date   27/05/25
/// @brief  Wrapper of the Tracy Profiler

#pragma once
#ifdef NDEBUG
#ifndef TOAST_EDITOR    // In release mode, disable Tracy unless we're in the editor
#undef TRACY_ENABLE
#endif
#endif

///@note Tracy might appear as leaking memory, this is because its saving the trace until the server connects
#ifdef TRACY_ENABLE
// #undef TRACY_FIBERS
// #define TRACY_DELAYED_INIT
// #define TRACY_ON_DEMAND
#include <tracy/Tracy.hpp>

#define PROFILE_FRAME FrameMark
#define PROFILE_ZONE ZoneScoped
#define PROFILE_ZONE_N(name) ZoneScopedN(name)
#define PROFILE_ZONE_S(scope) ZoneScopedS(scope)
#define PROFILE_ZONE_C(color) ZoneScopedC(color)
#define PROFILE_ZONE_NC(name, color) ZoneScopedNC(name, color)
#define PROFILE_ZONE_NS(name, scope) ZoneScopedNS(name, scope)
#define PROFILE_ZONE_NCS(name, color, scope) ZoneScopedNCS(name, color, scope)

#define PROFILE_TEXT(name, size) ZoneText(name, size)
#define PROFILE_TEXT_F(fmt, ...) ZoneTextF(fmt, __VA_ARGS__)

#define PROFILE_MESSAGE(text, size) TracyMessage(text, size)
#define PROFILE_MESSAGE_C(text, size, color) TracyMessageC(text, size, color)

#define PROFILE_GPU_INIT()
#define PROFILE_GPU_INIT_NAMED(name)
#define PROFILE_GPU_COLLECT()
#define PROFILE_GPU_ZONE(name)

#else

/// Mark the end of a frame
#define PROFILE_FRAME

/// Scoped zone using default settings
#define PROFILE_ZONE

/// Scoped zone with a custom name
#define PROFILE_ZONE_N(name)

/// Scoped zone with a custom source scope
#define PROFILE_ZONE_S(scope)

/// Scoped zone with a specific color
#define PROFILE_ZONE_C(color)

/// Scoped zone with a name and color
#define PROFILE_ZONE_NC(name, color)

/// Scoped zone with a name and scope
#define PROFILE_ZONE_NS(name, scope)

/// Scoped zone with name, color, and scope
#define PROFILE_ZONE_NCS(name, color, scope)

/// Send a custom message to the profiler
#define PROFILE_MESSAGE(text, size)

/// Sets a custom text to the current zone
#define PROFILE_TEXT(name, size)

/// Sets a custom text with FMT formatting to the current zone
#define PROFILE_TEXT_F(fmt, ...)

/// Send a colored message to the profiler
#define PROFILE_MESSAGE_C(text, size, color)

#define PROFILE_GPU_INIT()
#define PROFILE_GPU_INIT_NAMED(name)
#define PROFILE_GPU_COLLECT()
#define PROFILE_GPU_ZONE(name)

#endif
