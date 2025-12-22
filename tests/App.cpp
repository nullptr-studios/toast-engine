#include "App.hpp"
#include "Engine/Toast/World.hpp"
#include "TestScene.hpp"

#include <Engine/Core/Log.hpp>

void Test::Begin() {
	toast::World::New<TestScene>("TestScene");
}
