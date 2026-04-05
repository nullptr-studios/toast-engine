#include "game.h"

#include <print>
#include <toast/application.hpp>

namespace {

class DummyGame : public toast::IApplication {
	void Begin() override { }
	void Tick() override { }
	void Destroy() override { }
};

}

game_t* game_create(void) {
	auto* game = new DummyGame();
	std::println("Hi from game!!!");
	// toast::pushApplicationLayer(game);
	return reinterpret_cast<game_t*>(game);
}
