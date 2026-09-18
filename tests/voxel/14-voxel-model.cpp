#include "test_registry.hpp"
#include "voxel_test_utils.hpp"

#include <array>
#include <cassert>
#include <cstring>
#include <string>
#include <toast/assets/asset_registry.hpp>
#include <toast/assets/voxel_model.hpp>

using namespace voxel;
using namespace voxeltest;
using assets::SaveMode;
using assets::VoxelModel;

namespace {

constexpr size_t k_version_offset = 6;
constexpr size_t k_dims_offset = 8;
constexpr size_t k_grid_offset = 28;

template<typename T>
void put(std::vector<uint8_t>& bytes, size_t offset, T value) {
	std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

[[nodiscard]]
auto sameContent(const Volume& a, const Volume& b) -> bool {
	bool same = a.brickDims() == b.brickDims();
	forEachCell(glm::ivec3(a.voxelDims()), [&](glm::ivec3 v) { same = same && a.materialAt(v) == b.materialAt(v); });
	return same;
}

[[nodiscard]]
auto rejects(const std::vector<uint8_t>& bytes, std::string_view reason = {}) -> bool {
	try {
		static_cast<void>(VoxelModel(bytes));
	} catch (const std::runtime_error& error) {
		return std::string_view(error.what()).find(reason) != std::string_view::npos;
	}
	return false;
}

}

TOAST_TEST_NAMED("voxel", "voxel/14-voxel-model", test_voxel_14_voxel_model) {
	{
		BrickPool pool(4);
		Volume source(pool, glm::uvec3(2, 1, 1));
		source.setVoxel(glm::ivec3(0), 3);
		Volume volume = Volume::instanceOf(source);

		std::array<uint8_t, k_brick_material_bytes> bytes {};
		for (size_t i = 0; i < bytes.size(); ++i) {
			bytes[i] = static_cast<uint8_t>(i % 3 == 0 ? 0 : 1 + i % 5);
		}
		assert(volume.setBrickMaterial(glm::ivec3(0), bytes));
		assert(volume.entryAt(glm::ivec3(0)).tag() == BrickTag::owned && source.materialAt(glm::ivec3(0)) == 3);
		for (uint32_t i = 0; i < k_brick_voxel_count; ++i) {
			assert(volume.materialAt(localVoxel(i)) == bytes[i] && volume.isSolidAt(localVoxel(i)) == (bytes[i] != 0));
		}

		bytes.fill(4);
		assert(volume.setBrickMaterial(glm::ivec3(0), bytes) && volume.entryAt(glm::ivec3(0)).tag() == BrickTag::uniform);
		bytes.fill(k_empty_palette_index);
		assert(volume.setBrickMaterial(glm::ivec3(0), bytes) && volume.entryAt(glm::ivec3(0)).tag() == BrickTag::empty);
		assert(pool.allocatedCount() == 1);
		assert(!volume.setBrickMaterial(glm::ivec3(5, 0, 0), bytes));
	}

	Rng rng {0x7B0C'5EED'0000'0009ull};
	BrickPool pool(64);
	Volume original(pool, glm::uvec3(3, 2, 2));
	original.setBrickUniform(glm::ivec3(0), 5);
	forEachCell(glm::ivec3(8), [&original](glm::ivec3 v) { original.setVoxel(v + glm::ivec3(8, 0, 0), 7); });
	fillRandom(original, glm::ivec3(2, 0, 0), rng, 20);
	fillRandom(original, glm::ivec3(0, 1, 0), rng, 80);
	fillRandom(original, glm::ivec3(1, 1, 1), rng, 30);

	const auto model = VoxelModel::capture(original, 0x1234'5678'9ABCull);
	assert(model->storedBrickCount() == 3 && model->brickDims() == original.brickDims());
	assert(model->paletteUid() == 0x1234'5678'9ABCull && model->solidVoxelCount() == original.solidVoxelCount());

	const std::vector<uint8_t> valid = model->serialize(SaveMode::game);
	{
		const VoxelModel loaded(valid);
		assert(loaded.paletteUid() == model->paletteUid() && loaded.storedBrickCount() == 3);
		assert(loaded.serialize(SaveMode::editor) == valid);

		BrickPool fresh(64);
		const std::optional<Volume> volume = loaded.instantiate(fresh);
		assert(volume.has_value() && sameContent(*volume, original) && fresh.allocatedCount() == 3);
		assert(volume->entryAt(glm::ivec3(1, 0, 0)).tag() == BrickTag::uniform);
		assert(VoxelModel::capture(*volume, loaded.paletteUid())->serialize(SaveMode::game) == valid);
		assert(VoxelModel::capture(Volume::instanceOf(original), model->paletteUid())->serialize(SaveMode::game) == valid);

		BrickPool tiny(2);
		assert(!loaded.instantiate(tiny).has_value() && tiny.allocatedCount() == 0);
	}

	{
		BrickPool empty_pool(1);
		const std::vector<uint8_t> bytes = VoxelModel::capture(Volume(empty_pool, glm::uvec3(2, 2, 2)), 0)->serialize(SaveMode::game);
		assert(bytes.size() == k_grid_offset + 8 * 4 && VoxelModel(bytes).storedBrickCount() == 0);
	}

	{
		const size_t slots = 3 * 2 * 2;
		const size_t bricks_offset = k_grid_offset + slots * 4;
		const auto slotWith = [&valid](BrickTag tag, size_t nth) {
			for (size_t slot = 0;; ++slot) {
				uint32_t value = 0;
				std::memcpy(&value, valid.data() + k_grid_offset + slot * 4, sizeof(value));
				if (BrickEntry {value}.tag() == tag && nth-- == 0) {
					return k_grid_offset + slot * 4;
				}
			}
		};
		const auto corrupt = [&valid](const auto& change) {
			std::vector<uint8_t> bytes = valid;
			change(bytes);
			return bytes;
		};

		assert(!rejects(valid));
		assert(rejects({}, "truncated"));
		assert(rejects(corrupt([](auto& b) { b.resize(10); }), "truncated"));
		assert(rejects(corrupt([](auto& b) { b.pop_back(); }), "truncated"));
		assert(rejects(corrupt([](auto& b) { b.push_back(0); }), "trailing"));
		assert(rejects(corrupt([](auto& b) { b[0] = 'X'; }), "not a .tvox"));
		assert(rejects(corrupt([](auto& b) { put<uint16_t>(b, k_version_offset, 0); }), "reimport"));
		assert(rejects(corrupt([](auto& b) { put<uint16_t>(b, k_version_offset, 2); }), "reimport"));
		assert(rejects(corrupt([](auto& b) { put<uint16_t>(b, k_dims_offset, 0); })));
		assert(rejects(corrupt([](auto& b) { std::memset(b.data() + k_dims_offset, 0xEA, 6); }), "truncated"));

		const size_t empty = slotWith(BrickTag::empty, 0);
		const size_t uniform = slotWith(BrickTag::uniform, 0);
		const size_t first_owned = slotWith(BrickTag::owned, 0);
		const size_t second_owned = slotWith(BrickTag::owned, 1);
		assert(rejects(corrupt([&](auto& b) { put(b, empty, BrickEntry::make(BrickTag::empty, 7).value); }), "empty entry"));
		assert(rejects(corrupt([&](auto& b) { put(b, uniform, BrickEntry::make(BrickTag::uniform, 0).value); }), "uniform"));
		assert(rejects(corrupt([&](auto& b) { put(b, uniform, BrickEntry::make(BrickTag::uniform, 300).value); }), "uniform"));
		assert(rejects(corrupt([&](auto& b) { put(b, first_owned, BrickEntry::make(BrickTag::shared, 0).value); }), "shared"));
		assert(rejects(
		    corrupt([&](auto& b) {
			    put(b, first_owned, BrickEntry::make(BrickTag::owned, 1).value);
			    put(b, second_owned, BrickEntry::make(BrickTag::owned, 0).value);
		    }),
		    "file order"
		));
		for (uint8_t fill : {uint8_t {0}, uint8_t {9}}) {
			assert(rejects(corrupt([&](auto& b) { std::memset(b.data() + bricks_offset, fill, k_brick_material_bytes); }), "single material"));
		}
	}

	assets::AssetRegistry::init();
	assert(assets::AssetRegistry::createRaw("voxel_model", valid)->type() == "voxel_model");
}
