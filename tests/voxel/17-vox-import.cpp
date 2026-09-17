#include "test_registry.hpp"
#include "vox_builder.hpp"
#include "voxel_test_utils.hpp"

#include <array>
#include <cassert>
#include <set>
#include <string>
#include <toast/assets/vox_import.hpp>
#include <vector>

using namespace assets;
using toast::voxel::LatticeOrientation;
using voxbuild::Builder;
using voxeltest::throws;

TOAST_TEST_NAMED("voxel", "voxel/17-vox-import", test_voxel_17_vox_import) {
	const std::array<LatticeOrientation, 48> all = toast::voxel::allLatticeOrientations();
	LatticeOrientation quarter;
	quarter.source = {1, 0, 2};
	quarter.flip = {true, false, false};

	{
		std::set<uint8_t> bytes;
		for (const LatticeOrientation& orientation : all) {
			bytes.insert(voxOrientationToByte(orientation));
			assert(voxOrientationFromByte(voxOrientationToByte(orientation)) == orientation);
		}
		assert(bytes.size() == 48 && voxOrientationToByte(LatticeOrientation {}) == 4);

		uint32_t accepted = 0;
		for (uint32_t byte = 0; byte < 128; ++byte) {
			accepted += voxOrientationFromByte(static_cast<uint8_t>(byte)).has_value() ? 1 : 0;
			assert(voxOrientationFromByte(static_cast<uint8_t>(byte)) == voxOrientationFromByte(static_cast<uint8_t>(byte | 0x80u)));
		}
		assert(accepted == 48);
	}

	for (const LatticeOrientation& parent : all) {
		for (const LatticeOrientation& local : all) {
			const VoxTransform composed = composeVoxTransforms(VoxTransform {.orientation = parent}, VoxTransform {.orientation = local});
			assert(toMatrix(composed.orientation) == toMatrix(parent) * toMatrix(local));
		}
	}
	assert(
	    composeVoxTransforms(VoxTransform {quarter, glm::ivec3(10, 0, 0)}, VoxTransform {LatticeOrientation {}, glm::ivec3(4, 0, 0)}).translation ==
	    glm::ivec3(10, 4, 0)
	);

	for (uint32_t dim : {8u, 5u}) {
		for (const LatticeOrientation& orientation : all) {
			const VoxTransform transform {orientation, glm::ivec3(7, -3, 20)};
			const toast::voxel::LatticePlacement placement = voxPlacementOf(transform, glm::uvec3(dim));
			voxeltest::forEachCell(glm::ivec3(static_cast<int32_t>(dim)), [&](glm::ivec3 v) {
				const glm::vec3 centred = glm::vec3(v) + 0.5f - static_cast<float>(dim) * 0.5f;
				assert(placeVoxel(placement, v) == glm::ivec3(glm::floor(toMatrix(orientation) * centred)) + transform.translation);
			});
		}
	}

	{
		Builder builder;
		builder.size(3, 4, 5);
		builder.xyzi({{0, 1, 2, 1}, {2, 3, 4, 255}, {1, 1, 1, 0}});
		builder.rgba();
		const VoxScene scene = importVox(builder.file());

		assert(scene.models.size() == 1 && scene.models[0].dims == glm::uvec3(3, 4, 5) && scene.models[0].voxels.size() == 2);
		assert(scene.models[0].voxels[1].palette_index == 255 && scene.nodes.empty());
		assert(scene.palette.entries[1].albedo_g == 255 && scene.palette.entries[255].albedo_r == 254);
		assert(scene.palette.entries[0] == toast::voxel::PaletteEntry {});

		const std::vector<VoxPlacement> placed = flattenVoxScene(scene);
		assert(placed.size() == 1 && placed[0].placement.offset == glm::ivec3(-1, -2, -2));
	}

	{
		Builder builder = Builder::singleVoxel(2, 2, 2);
		builder.size(4, 4, 4);
		builder.xyzi({{1, 1, 1, 4}});
		builder.ntrn(0, 1, {}, {});
		builder.ngrp(1, {2, 4});
		builder.ntrn(2, 3, {{"_name", "small"}}, {{"_t", "0 0 0"}});
		builder.nshp(3, 0);
		builder.ntrn(4, 5, {{"_name", "big"}, {"_hidden", "1"}}, {{"_t", "100 0 0"}});
		builder.nshp(5, 1);

		const VoxScene scene = importVox(builder.file(200));
		assert(scene.nodes.size() == 6 && scene.warnings.empty());
		const std::vector<VoxPlacement> placed = flattenVoxScene(scene);
		assert(placed.size() == 2);
		assert(placed[0].name == "small" && placed[0].model == 0 && !placed[0].hidden && placed[0].placement.offset == glm::ivec3(-1));
		assert(placed[1].name == "big" && placed[1].model == 1 && placed[1].hidden && placed[1].placement.offset == glm::ivec3(98, -2, -2));
	}

	{
		Builder builder = Builder::singleVoxel(2, 2, 2);
		builder.ntrn(0, 1, {}, {{"_r", std::to_string(voxOrientationToByte(quarter))}});
		builder.ngrp(1, {2});
		builder.ntrn(2, 3, {}, {{"_t", "10 0 0"}});
		builder.nshp(3, 0);

		const std::vector<VoxPlacement> placed = flattenVoxScene(importVox(builder.file()));
		assert(placed.size() == 1 && placed[0].placement.orientation == quarter && placed[0].placement.offset == glm::ivec3(1, 9, -1));
	}

	{
		Builder builder = Builder::singleVoxel(2, 2, 2);
		builder.ntrn(0, 1, {}, {{"_r", "5"}});
		builder.nshp(1, 0);
		const VoxScene scene = importVox(builder.file());
		assert(scene.warnings.size() == 1 && scene.nodes[0].local.orientation == LatticeOrientation {});
	}

	{
		Builder builder = Builder::singleVoxel(1, 1, 1);
		builder.matl(1, {{"_type", "_metal"}, {"_rough", "0.25"}, {"_metal", "1"}});
		builder.matl(2, {{"_type", "_emit"}, {"_emit", "1"}, {"_flux", "2"}});
		builder.matl(3, {{"_type", "_emit"}, {"_emit", "0.5"}, {"_flux", "0"}});
		builder.matl(4, {{"_type", "_glass"}, {"_alpha", "0.4"}});
		builder.matl(300, {{"_rough", "1"}});

		const toast::voxel::Palette palette = importVox(builder.file(200)).palette;
		assert(palette.entries[1].roughness == 64 && palette.entries[1].metallic == 255);
		assert(palette.max_emissive == 4.0f && palette.entries[2].emissive == 255 && palette.entries[3].emissive == 32);
		assert((palette.entries[4].flags & toast::voxel::k_entry_transparent) != 0);
		assert(importVox(builder.file(200)).warnings.size() == 1);
		assert(importVox(Builder::singleVoxel(1, 1, 1).file()).palette.max_emissive == 1.0f);
	}

	{
		Builder no_rgba;
		no_rgba.size(1, 1, 1);
		no_rgba.xyzi({{0, 0, 0, 1}});
		const VoxScene scene = importVox(no_rgba.file());
		assert(scene.palette.entries[1].albedo_r == 200 && scene.warnings.size() == 1);

		Builder unknown_chunk = Builder::singleVoxel(1, 1, 1);
		const size_t start = unknown_chunk.body.size();
		unknown_chunk.i32(1234);
		unknown_chunk.chunk("zZzZ", start);
		assert(importVox(unknown_chunk.file(200)).warnings.size() == 1);
	}

	{
		Builder builder;
		builder.size(9, 3, 3);
		builder.xyzi({{0, 0, 0, 1}, {8, 2, 2, 2}});
		const VoxScene scene = importVox(builder.file());

		toast::voxel::BrickPool pool(4);
		const std::optional<toast::voxel::Volume> volume = buildVoxVolume(scene.models[0], pool);
		assert(volume.has_value() && volume->brickDims() == glm::uvec3(2, 1, 1) && volume->solidVoxelCount() == 2);
		assert(volume->materialAt(glm::ivec3(8, 2, 2)) == 2);

		toast::voxel::BrickPool exhausted(1);
		assert(!buildVoxVolume(scene.models[0], exhausted).has_value() && exhausted.allocatedCount() == 0);
	}

	{
		const auto refuses = [](const Builder& builder, int32_t version = 150) {
			return throws([&] { static_cast<void>(importVox(builder.file(version))); });
		};
		const Builder good = Builder::singleVoxel(1, 1, 1);
		assert(!refuses(good) && refuses(good, 999));
		assert(throws([] { static_cast<void>(importVox({})); }));
		assert(throws([] { static_cast<void>(importVox(std::vector<uint8_t> {'N', 'O', 'P', 'E', 150, 0, 0, 0})); }));

		std::vector<uint8_t> truncated = good.file();
		truncated.resize(truncated.size() - 4);
		assert(throws([&truncated] { static_cast<void>(importVox(truncated)); }));

		Builder outside;
		outside.size(2, 2, 2);
		outside.xyzi({{5, 0, 0, 1}});
		Builder orphan;
		orphan.xyzi({{0, 0, 0, 1}});
		Builder oversized;
		oversized.size(257, 1, 1);
		Builder flat;
		flat.size(0, 1, 1);
		Builder dangling = Builder::singleVoxel(1, 1, 1);
		dangling.nshp(0, 3);
		Builder two_roots = Builder::singleVoxel(1, 1, 1);
		two_roots.ntrn(0, 1, {}, {});
		two_roots.nshp(1, 0);
		two_roots.ntrn(2, 3, {}, {});
		two_roots.nshp(3, 0);
		Builder cycle = Builder::singleVoxel(1, 1, 1);
		cycle.ntrn(0, 1, {}, {});
		cycle.ntrn(1, 0, {}, {});
		for (const Builder* bad : {&outside, &orphan, &oversized, &flat, &dangling, &two_roots, &cycle}) {
			assert(refuses(*bad));
		}
	}
}
