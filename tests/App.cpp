#include "App.hpp"

#include <Engine/Core/Log.hpp>
#include <Engine/Toast/Objects/Scene.hpp>
#include <Engine/Toast/World.hpp>

void Test::Begin() {
	toast::World::LoadSceneSync("scenes/TestScene.scene");
}
