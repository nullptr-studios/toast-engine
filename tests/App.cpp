#include "App.hpp"

#include <Engine/Core/Log.hpp>
#include <Engine/Toast/Objects/Scene.hpp>
#include <Engine/Toast/World.hpp>

void Test::Begin() {
	toast::World::LoadScene("scenes/TestScene.scene", [](auto* s) {
		s->enabled(true);
	});
}
