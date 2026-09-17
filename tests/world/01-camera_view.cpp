#include "test_registry.hpp"

#include <cassert>
#include <cmath>
#include <toast/world/camera.hpp>

TOAST_TEST_NAMED("World", "world/01-camera_view", test_world_01_camera_view) {
	toast::Camera camera;
	camera.position = {0.0f, 0.0f, 5.0f};

	const glm::mat4 view = camera.getView();
	for (int column = 0; column < 4; ++column) {
		for (int row = 0; row < 4; ++row) {
			assert(std::isfinite(view[column][row]));
		}
	}
}
