#include "App.hpp"

#include <Toast/Log.hpp>
#include <Toast/Objects/Scene.hpp>
#include <Toast/World.hpp>

void Test::Begin() {
	toast::World::LoadSceneSync("scenes/TestScene.scene");
}
