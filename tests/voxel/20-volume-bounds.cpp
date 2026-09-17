#include "test_registry.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <toast/voxel/volume_bounds.hpp>

using namespace voxel;

TOAST_TEST_NAMED("voxel", "voxel/20-volume-bounds", test_voxel_20_volume_bounds) {
	const auto near = [](float lhs, float rhs) { return std::abs(lhs - rhs) < 1e-4f; };
	const glm::uvec3 dims(2, 3, 1);

	assert(near(localExtent(dims).x, 1.6f) && near(localExtent(dims).y, 2.4f) && near(localExtent(dims).z, 0.8f));

	{
		const glm::vec4 sphere = worldBoundingSphere(glm::mat4(1.0f), dims);
		assert(near(sphere.x, 0.8f) && near(sphere.y, 1.2f) && near(sphere.z, 0.4f));
		assert(near(sphere.w, glm::length(localExtent(dims)) * 0.5f));
	}

	{
		glm::mat4 shear(1.0f);
		shear[1][0] = 0.7f;

		const std::array<glm::mat4, 4> transforms {
		  glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, -4.0f, 2.0f)),
		  glm::rotate(glm::mat4(1.0f), glm::radians(37.0f), glm::normalize(glm::vec3(1.0f, 2.0f, 3.0f))),
		  glm::scale(glm::mat4(1.0f), glm::vec3(3.0f, 0.5f, 1.0f)),
		  glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, 5.0f, 1.0f)) * shear *
		      glm::rotate(glm::mat4(1.0f), glm::radians(80.0f), glm::vec3(0.0f, 0.0f, 1.0f)) *
		      glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 4.0f, 2.0f)),
		};

		for (const glm::mat4& model : transforms) {
			const glm::vec4 sphere = worldBoundingSphere(model, dims);
			const glm::vec3 extent = localExtent(dims);

			float farthest = 0.0f;
			for (int corner = 0; corner < 8; ++corner) {
				const glm::vec3 local((corner & 1) ? extent.x : 0.0f, (corner & 2) ? extent.y : 0.0f, (corner & 4) ? extent.z : 0.0f);
				const float distance = glm::length(glm::vec3(model * glm::vec4(local, 1.0f)) - glm::vec3(sphere));
				assert(distance <= sphere.w + 1e-4f);
				farthest = std::max(farthest, distance);
			}
			assert(near(farthest, sphere.w));
		}
	}

	{
		const glm::mat4 inverse(1.0f);
		assert(containsPoint(inverse, dims, glm::vec3(0.8f, 1.2f, 0.4f)));
		assert(containsPoint(inverse, dims, glm::vec3(0.0f)));
		assert(!containsPoint(inverse, dims, glm::vec3(-0.05f, 1.0f, 0.4f)));
		assert(!containsPoint(inverse, dims, glm::vec3(0.8f, 1.2f, 0.85f)));
		assert(containsPoint(inverse, dims, glm::vec3(-0.05f, 1.0f, 0.4f), 0.1f));
		assert(!containsPoint(inverse, dims, glm::vec3(-0.15f, 1.0f, 0.4f), 0.1f));
	}

	{
		const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 5.0f, 0.0f)) *
		                        glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)) *
		                        glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
		const glm::mat4 inverse = glm::inverse(model);

		const auto world = [&model](glm::vec3 local) { return glm::vec3(model * glm::vec4(local, 1.0f)); };
		assert(containsPoint(inverse, dims, world(glm::vec3(0.8f, 1.2f, 0.4f))));
		assert(containsPoint(inverse, dims, world(glm::vec3(1.59f, 2.39f, 0.79f))));
		assert(!containsPoint(inverse, dims, world(glm::vec3(1.7f, 1.2f, 0.4f))));
		assert(!containsPoint(inverse, dims, world(glm::vec3(0.8f, -0.1f, 0.4f))));
	}

	{
		const glm::mat4 model = glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
		const glm::mat4 inverse = glm::inverse(model);
		const glm::vec3 outside_local(-0.08f, 1.0f, 0.4f);
		const glm::vec3 outside_world = glm::vec3(model * glm::vec4(outside_local, 1.0f));
		assert(!containsPoint(inverse, dims, outside_world));
		assert(containsPoint(inverse, dims, outside_world, 0.05f));
		assert(!containsPoint(inverse, dims, outside_world, 0.03f));
	}
}
