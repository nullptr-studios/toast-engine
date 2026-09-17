#include "test_registry.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <toast/voxel/palette.hpp>
#include <type_traits>

using namespace voxel;

namespace {

[[nodiscard]]
auto concrete() -> PhysicalMaterial {
	PhysicalMaterial m;
	m.density = 2400;
	m.toughness = 500.0f;
	m.structural_strength = 0.2f;
	return m;
}

[[nodiscard]]
auto reports(const std::vector<TableProblem>& actual, std::initializer_list<TableProblem> expected) -> bool {
	return std::ranges::equal(actual, expected);
}

}

TOAST_TEST_NAMED("voxel", "voxel/12-palette-tables", test_voxel_12_palette_tables) {
	static_assert(sizeof(PaletteEntry) == 16 && std::is_trivially_copyable_v<PaletteEntry>);
	static_assert(offsetof(PaletteEntry, roughness) == 3 && offsetof(PaletteEntry, metallic) == 4);
	static_assert(offsetof(PaletteEntry, emissive) == 6 && offsetof(PaletteEntry, material) == 7);
	static_assert(offsetof(PaletteEntry, flags) == 8 && offsetof(PaletteEntry, transforms_to) == 9);

	assert(std::abs(massPerVoxel(concrete()) - 2.4f) < 1e-4f);

	Palette palette;
	palette.max_emissive = 64.0f;
	palette.entries[6].emissive = 51;
	assert(std::abs(emissiveIntensity(palette, 6) - 12.8f) < 1e-4f && emissiveIntensity(palette, 7) == 0.0f);

	PhysicalMaterial foliage;
	assert(foliage.collides() && !foliage.isFlammable() && !foliage.isIndestructible());
	foliage.flags = k_material_passable | k_material_flammable;
	assert(!foliage.collides() && foliage.isFlammable());

	MaterialLibrary library;
	library.materials = {concrete(), concrete()};
	assert(validateLibrary(library).empty());

	palette.entries[10].material = 1;
	palette.entries[11].material = 2;
	assert(resolveMaterialIndex(palette, library, 10) == 1);
	assert(resolveMaterialIndex(palette, library, 11) == k_default_material);
	assert(reports(validatePalette(palette, library), {{TableIssue::unknown_material, 11}}));

	palette.entries[0].albedo_r = 200;
	assert(reports(validatePalette(palette, library), {{TableIssue::reserved_slot_used, 0}, {TableIssue::unknown_material, 11}}));
	assert(reports(validatePalette(Palette {}, MaterialLibrary {}), {{TableIssue::library_empty, 0}}));

	MaterialLibrary broken;
	broken.materials.assign(6, concrete());
	broken.materials[1].density = 0;
	broken.materials[2].static_friction = -0.1f;
	broken.materials[3].restitution = 1.5f;
	broken.materials[4].toughness = std::numeric_limits<float>::quiet_NaN();
	broken.materials[5].flags = k_material_indestructible;
	broken.materials[5].toughness = 0.0f;
	assert(reports(
	    validateLibrary(broken),
	    {{TableIssue::zero_density, 1},
	     {TableIssue::out_of_range, 2},
	     {TableIssue::out_of_range, 3},
	     {TableIssue::out_of_range, 4},
	     {TableIssue::indestructible_with_zero_toughness, 5}}
	));

	assert(reports(validateLibrary(MaterialLibrary {}), {{TableIssue::library_empty, 0}}));
	broken.materials.assign(k_max_physical_materials + 1, concrete());
	assert(reports(validateLibrary(broken), {{TableIssue::library_too_large, k_max_physical_materials + 1}}));
}
