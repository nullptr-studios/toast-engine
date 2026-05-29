/**
 * Toast Engine (c) 2026
 * THIS IS AN INTERNAL FILE, DO NOT MODIFY UNLESS YOU KNOW WHAT YOU ARE DOING
 * THIS FILE SHOULD DECLARE A game_t* game_create(void) FOR THE CORRECT WORKING OF THE APPLICATION
 */

#pragma once

#if defined(_WIN32)
#if defined(GAME_EXPORT)
#define GAME_API __declspec(dllexport)
#else
#define GAME_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#define GAME_API __attribute__((visibility("default")))
#else
#define GAME_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct game_t;                         ///< Opaque Engine ptr

GAME_API game_t* game_create(void);    ///< Initializes the game engine

#ifdef __cplusplus
}
#endif
