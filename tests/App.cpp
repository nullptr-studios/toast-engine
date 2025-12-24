#include "App.hpp"

#include "Engine/Toast/World.hpp"
#include "TestScene.hpp"

#include <Engine/Core/Log.hpp>

void Test::Begin() {
	toast::World::New("TestScene", "Test_Scene", std::nullopt, [](toast::Scene* s) {
		s->enabled(true);
	});
}
