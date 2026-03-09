#include "App.hpp"

#include <Toast/Log.hpp>
#include <Toast/Nodes/RootNode.hpp>
#include <Toast/World.hpp>

void Test::Begin() {
	toast::World::LoadRootNodeSync("SCENES/TestRootNode.scene");
}
