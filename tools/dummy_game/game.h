/// @file game.h
/// @author Xein
/// @date 17 Feb 2026

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

struct game_t; ///< Opaque Engine ptr

GAME_API game_t* game_create(void);     ///< Initializes the game engine

#ifdef __cplusplus
}
#endif