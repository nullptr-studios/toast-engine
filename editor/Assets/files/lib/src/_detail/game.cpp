/**
 * Toast Engine (c) 2026
 * THIS IS AN INTERNAL FILE, DO NOT MODIFY UNLESS YOU KNOW WHAT YOU ARE DOING
 * THIS FILE SHOULD DECLARE A game_t* game_create(void) FOR THE CORRECT WORKING OF THE APPLICATION
 */

#include "game.h"
#include "../my_game.hpp"

game_t* game_create(void) {
	auto* game = new MyGame();
	toast::pushApplicationLayer(game);
	return reinterpret_cast<game_t*>(game);
}
