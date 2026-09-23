#include "test_registry.hpp"

#include <cassert>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <toast/assets/animation.hpp>

namespace {

auto nearly(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) -> bool {
	return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps;
}

}

// Skeletal skinning is the engine's only mesh deformation path now that morph targets are gone, so the
// contract Skin::jointMatrices() offers the renderer has to hold in the awkward cases too: it is indexed
// directly by joint id in the vertex shader, with no bounds check anywhere between here and the GPU
TOAST_TEST_NAMED("Assets", "assets/06-skin-binding", test_assets_06_skin_binding) {
	assets::Skin skin;
	skin.name = "Rig";
	skin.joints = {"Root", "Spine", "Head"};

	// Bind pose: each joint one unit further along +X. The inverse bind undoes exactly that, so a joint left
	// at its bind position must contribute no deformation at all
	skin.inverse_bind_matrices = {
	  glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)),
	  glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, 0.0f, 0.0f)),
	  glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, 0.0f, 0.0f)),
	};

	const std::vector<glm::mat4> bind_pose {
	  glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)),
	  glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
	  glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f)),
	};

	{
		const auto matrices = skin.jointMatrices(bind_pose);
		assert(matrices.size() == 3);
		// Identity everywhere: a character standing in bind pose must render exactly as authored, not
		// smeared by a frame of accumulated error
		for (const auto& matrix : matrices) {
			assert(nearly(glm::vec3(matrix[3]), glm::vec3(0.0f)));
			assert(nearly(glm::vec3(matrix[0]), glm::vec3(1.0f, 0.0f, 0.0f)));
		}
	}

	{
		// Head lifted 3 units: only that joint's matrix may move, or one animated bone would drag the whole
		// mesh with it
		std::vector<glm::mat4> posed = bind_pose;
		posed[2] = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 3.0f, 0.0f));

		const auto matrices = skin.jointMatrices(posed);
		assert(nearly(glm::vec3(matrices[0][3]), glm::vec3(0.0f)));
		assert(nearly(glm::vec3(matrices[1][3]), glm::vec3(0.0f)));
		assert(nearly(glm::vec3(matrices[2][3]), glm::vec3(0.0f, 3.0f, 0.0f)));
	}

	{
		// Fewer world transforms than joints - what a clip that doesn't resolve every joint node produces.
		// The result still has to be one entry per joint: the shader indexes it by joint id, so a short
		// array would have later joints reading whatever follows in the pool
		const std::vector<glm::mat4> partial {bind_pose[0]};
		const auto matrices = skin.jointMatrices(partial);
		assert(matrices.size() == skin.joints.size());
		assert(nearly(glm::vec3(matrices[0][3]), glm::vec3(0.0f)));
		// Unresolved joints fall back to identity world transforms, which leaves the inverse bind exposed
		assert(nearly(glm::vec3(matrices[1][3]), glm::vec3(-1.0f, 0.0f, 0.0f)));
	}

	{
		// A skin with no inverse bind matrices at all is legal glTF (identity is implied), and has to pose
		// rather than produce an empty array the renderer would treat as "not skinned"
		assets::Skin bare;
		bare.joints = {"A", "B"};
		const std::vector<glm::mat4> world {
		  glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 0.0f, 0.0f)),
		  glm::mat4(1.0f),
		};
		const auto matrices = bare.jointMatrices(world);
		assert(matrices.size() == 2);
		assert(nearly(glm::vec3(matrices[0][3]), glm::vec3(4.0f, 0.0f, 0.0f)));
	}
}
