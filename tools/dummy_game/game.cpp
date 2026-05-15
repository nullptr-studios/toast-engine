#include "game.h"

#include <print>
#include <toast/application.hpp>

namespace {

class DummyGame : public toast::IApplication {
	void begin() override { }

	void tick() override { }

	void destroy() override { }
};

}

game_t* game_create(void) {
	auto* game = new DummyGame();
	//std::println("Hi from game!!!");
	toast::pushApplicationLayer(game);
	return reinterpret_cast<game_t*>(game);
}
