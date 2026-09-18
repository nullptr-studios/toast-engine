#include "test_registry.hpp"
#include "toast/world/world_test_access.hpp"

#include <cassert>
#include <memory>
#include <sstream>
#include <toast/assets/prefab.hpp>
#include <toast/assets/voxel_material_library.hpp>
#include <toast/assets/voxel_model.hpp>
#include <toast/assets/voxel_palette.hpp>
#include <toast/physics/simulator.hpp>
#include <toast/voxel/runtime_pool.hpp>
#include <toast/world/voxel_node.hpp>
#include <toml++/toml.hpp>

using namespace toast;
using WorldTestAccess = toast::_detail::WorldTestAccess;

TOAST_TEST_NAMED("voxel", "voxel/22-physics-ownership", test_voxel_22_physics_ownership) {
	const UID library_uid = UID::fromString("VoxLIBRARY0");
	const auto library = assets::VoxelMaterialLibrary::fromToml(toml::parse("[[materials]]\nname = \"stone\"\ndensity = 2400\n"));
	assets::VoxelPalette plain(voxel::Palette {}, library_uid.data(), {});
	voxel::Palette tinted_entries;
	tinted_entries.entries[1].albedo_r = 200;
	assets::VoxelPalette tinted(tinted_entries, library_uid.data(), {});

	// Declared before the world so they outlive the node handle
	voxel::BrickPool source_pool(4);
	voxel::Volume crate(source_pool, glm::uvec3(1, 1, 1));
	crate.setVoxel(glm::ivec3(1, 2, 3), 1);
	crate.setVoxel(glm::ivec3(4), 1);
	const auto crate_model = assets::VoxelModel::capture(crate, 0);
	voxel::Volume beam(source_pool, glm::uvec3(2, 1, 1));
	beam.setVoxel(glm::ivec3(0), 1);
	beam.setVoxel(glm::ivec3(9, 0, 0), 1);
	const auto beam_model = assets::VoxelModel::capture(beam, 0);

	std::stringstream text("[crate type=toast::VoxelNode]\nm_uid @uid = VoxelCRATE0\n");
	assets::Prefab prefab(text);
	assets::Handle<assets::Prefab> handle(&prefab, UID::fromString("VoxelSCENE0"), "");

	auto world = WorldTestAccess::createWorld();
	INodeOwner::InstantiateContext ctx;
	ctx.resolver = [](UID) { return assets::Handle<assets::Prefab> {}; };
	Box<Node> root = WorldTestAccess::instantiate(*world, handle, ctx);
	Box<VoxelNode> node = root.as<VoxelNode>();
	assert(node.exists());

	// setModel and setPalette drop the resolved library so it is handed back after each
	const auto setLibrary = [&] {
		WorldTestAccess::setVoxelMaterialLibrary(*node, assets::Handle<assets::VoxelMaterialLibrary>(library.get(), library_uid, ""));
	};
	node->setModel(assets::Handle<assets::VoxelModel>(crate_model.get(), UID::fromString("ModelCRATE0"), ""));
	node->setPalette(assets::Handle<assets::VoxelPalette>(&plain, UID::fromString("PaletteONE0"), ""));
	setLibrary();

	voxel::BrickPool& runtime = voxel::runtimeBrickPool();
	const uint32_t baseline = runtime.allocatedCount();
	{
		physics::Simulator simulator;

		const voxel::Volume* drawn = node->volume();
		assert(drawn != nullptr && runtime.allocatedCount() == baseline + 1);

		physics::Simulator::registerVoxelNode(*node);
		voxel::Volume* simulated = node->volume();
		assert(simulated != nullptr && simulated->materialAt(glm::ivec3(4)) == 1);
		assert(runtime.allocatedCount() == baseline + 1);

		const uint32_t node_revision = node->revision();
		simulated->setVoxel(glm::ivec3(4), voxel::k_empty_palette_index);
		simulator.tick();
		assert(node->volume() == simulated && simulated->materialAt(glm::ivec3(4)) == voxel::k_empty_palette_index);
		assert(node->revision() == node_revision && runtime.allocatedCount() == baseline + 1);

		node->setPalette(assets::Handle<assets::VoxelPalette>(&tinted, UID::fromString("PaletteTWO0"), ""));
		setLibrary();
		simulator.tick();
		assert(node->volume() == simulated && simulated->materialAt(glm::ivec3(4)) == voxel::k_empty_palette_index);

		node->setModel(assets::Handle<assets::VoxelModel>(beam_model.get(), UID::fromString("ModelBEAM00"), ""));
		setLibrary();
		assert(node->volume()->brickDims() == glm::uvec3(2, 1, 1));
		assert(runtime.allocatedCount() == baseline + 3);
		simulator.tick();
		assert(node->volume()->brickDims() == glm::uvec3(2, 1, 1) && runtime.allocatedCount() == baseline + 2);

		physics::Simulator::unregisterVoxelNode(*node);
		assert(node->volume() != nullptr && runtime.allocatedCount() == baseline + 2);

		physics::Simulator::registerVoxelNode(*node);
		assert(runtime.allocatedCount() == baseline + 2);
	}
	assert(node->volume() != nullptr && node->volume()->materialAt(glm::ivec3(9, 0, 0)) == 1);
	assert(runtime.allocatedCount() == baseline + 2);

	node->setModel({});
	assert(node->volume() == nullptr && runtime.allocatedCount() == baseline);
}
