#include "test_registry.hpp"
#include "toast/world/world_test_access.hpp"

#include <cassert>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <toast/assets/prefab.hpp>
#include <toast/assets/voxel_model.hpp>
#include <toast/assets/voxel_palette.hpp>
#include <toast/voxel/runtime_pool.hpp>
#include <toast/world/voxel_node.hpp>

using namespace toast;
using WorldTestAccess = toast::_detail::WorldTestAccess;

TOAST_TEST_NAMED("voxel", "voxel/16-voxel-node", test_voxel_16_voxel_node) {
	const auto near = [](glm::vec3 lhs, glm::vec3 rhs) { return glm::all(glm::lessThan(glm::abs(lhs - rhs), glm::vec3(1e-5f))); };

	// Declared before the world so it outlives the node handle
	voxel::BrickPool pool(4);
	voxel::Volume volume(pool, glm::uvec3(2, 3, 1));
	volume.setVoxel(glm::ivec3(1, 2, 3), 1);
	volume.setVoxel(glm::ivec3(9, 17, 0), 2);
	const std::unique_ptr<assets::VoxelModel> model = assets::VoxelModel::capture(volume, 0xABCD'0000'0042ull);

	std::stringstream ss("[piece type=toast::VoxelNode]\nm_uid @uid = VoxelPIECE0\nposition @vec3 = 0.3, -1.2, 25\nm_mobility @int = 1\n");
	assets::Prefab prefab(ss);
	assets::Handle<assets::Prefab> handle(&prefab, UID::fromString("VoxelSCENE0"), "");

	auto world = WorldTestAccess::createWorld();
	INodeOwner::InstantiateContext ctx;
	ctx.resolver = [](UID) { return assets::Handle<assets::Prefab> {}; };
	Box<Node> root = WorldTestAccess::instantiate(*world, handle, ctx);
	Box<VoxelNode> node = root.as<VoxelNode>();
	assert(node.exists() && node->mobility() == VoxelMobility::dynamic);

	for (const char* field : {"m_model", "m_palette", "m_mobility", "position"}) {
		assert(node->info()->search(field) != nullptr);
	}
	assert(node->info()->search("m_mobility")->hasAttribute("Enum"));

	assert(node->localBoundingSphere() == glm::vec4(0.0f) && node->paletteUid() == 0);
	node->setModel(assets::Handle<assets::VoxelModel>(model.get(), UID::fromString("VoxelMODEL0"), ""));
	const glm::vec4 sphere = node->localBoundingSphere();
	assert(near(glm::vec3(sphere), glm::vec3(0.8f, 1.2f, 0.4f)) && std::abs(sphere.w - glm::length(glm::vec3(1.6f, 2.4f, 0.8f)) * 0.5f) < 1e-5f);

	assert(node->paletteUid() == 0xABCD'0000'0042ull);
	node->setPalette(assets::Handle<assets::VoxelPalette>(nullptr, UID(0x1234'5678ull), ""));
	assert(node->paletteUid() == 0x1234'5678ull && node->resolvedPalette() == nullptr);

	node->syncTransform();
	assert((node->latticePlacement() == voxel::LatticePlacement {voxel::LatticeOrientation {}, glm::ivec3(3, -12, 250)}));

	node->rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	node->syncTransform();
	assert(node->latticePlacement().has_value());
	assert((node->latticePlacement()->orientation == voxel::LatticeOrientation {{1, 0, 2}, {true, false, false}}));

	for (const auto& [position, rotation, scale] :
	     {std::tuple {glm::vec3(0.35f, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f)},
	      std::tuple {glm::vec3(0.0f), glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 0.0f, 1.0f)), glm::vec3(1.0f)},
	      std::tuple {glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(2.0f)}}) {
		node->position = position;
		node->rotation = rotation;
		node->scale = scale;
		node->syncTransform();
		assert(!node->latticePlacement().has_value());
	}

	const assets::Prefab saved(*node);
	assert(saved.nodes.size() == 1 && saved.nodes[0].find("m_mobility").has_value() && saved.nodes[0].find("m_model").has_value());

	{
		voxel::BrickPool& runtime = voxel::runtimeBrickPool();
		const uint32_t baseline = runtime.allocatedCount();

		voxel::Volume* live = node->volume();
		assert(live != nullptr && runtime.allocatedCount() > baseline);
		assert(live->solidVoxelCount() == 2 && live->materialAt(glm::ivec3(9, 17, 0)) == 2);

		const uint32_t revision = node->revision();
		assert(node->volume() == live && node->revision() == revision);

		node->setModel({});
		assert(node->volume() == nullptr && node->revision() != revision && runtime.allocatedCount() == baseline);
	}

	assets::VoxelPalette not_a_model(voxel::Palette {}, 0, {});
	node->setModel(assets::Handle<assets::VoxelModel>(&not_a_model, UID::fromString("VoxelSHARED"), ""));
	assert(node->volume() == nullptr && node->localBoundingSphere() == glm::vec4(0.0f));
	node->setModel({});
}
