/// @file engine.h
/// @author Xein
/// @date 10 Feb 2026

#pragma once
#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

struct engine_t; ///< Opaque Engine ptr

TOAST_C_API engine_t* toast_create(void);     ///< Initializes the game engine
TOAST_C_API void toast_tick(void);            ///< Frame logic for the game engine
TOAST_C_API int toast_should_close(void);     ///< @return 1 if the engine should close
TOAST_C_API void toast_destroy(engine_t*);    ///< Destroys the game engine

#ifdef __cplusplus
}
#endif
