/// @file engine.h
/// @author Xein
/// @date 10 Feb 2026

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct engine_t; ///< Opaque Engine ptr

engine_t* toast_create(void); ///< Initializes the game engine
void toast_tick(void); ///< Frame logic for the game engine
int toast_should_close(void); ///< @return 1 if the engine should close
void toast_destroy(engine_t*); ///< Destroys the game engine

#ifdef __cplusplus
}
#endif
