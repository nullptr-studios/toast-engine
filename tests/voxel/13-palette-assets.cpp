#include "test_registry.hpp"
#include "voxel_test_utils.hpp"

#include <cassert>
#include <string>
#include <string_view>
#include <toast/assets/asset_registry.hpp>
#include <toast/assets/voxel_material_library.hpp>
#include <toast/assets/voxel_palette.hpp>
#include <toast/uid.hpp>

using namespace toast::voxel;
using namespace voxeltest;
using assets::SaveMode;
using assets::VoxelMaterialLibrary;
using assets::VoxelPalette;

namespace {

constexpr std::string_view k_library_text = R"(
[[materials]]
name = "concrete"
density = 2400
toughness = 500.0
tag = "stone"
dust_colour = [180, 170, 160]

[[materials]]
name = "wood"
density = 600
flammable = true
static_friction = 0.7

[[materials]]
name = "bedrock"
density = 3000
toughness = 1e9
indestructible = true
)";

constexpr std::string_view k_palette_text = R"(
library = "LMNOPQRSTUV"
max_emissive = 64.0

[[entries]]
index = 1
albedo = [200, 120, 40]
roughness = 0.8
material = 1

[[entries]]
index = 7
albedo = [255, 180, 60]
reflectivity = 0.2
emissive = 1.0
transparent = true
transforms_to = 8
material = 0

[[entries]]
index = 9
albedo = [10, 200, 10]
)";

[[nodiscard]]
auto reparse(const std::vector<uint8_t>& bytes) -> toml::table {
	return toml::parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

template<typename Asset>
[[nodiscard]]
auto rejects(std::string_view text) -> bool {
	return throws([text] { static_cast<void>(Asset::fromToml(toml::parse(text))); });
}

}

TOAST_TEST_NAMED("voxel", "voxel/13-palette-assets", test_voxel_13_palette_assets) {
	const auto library = VoxelMaterialLibrary::fromToml(toml::parse(k_library_text));
	{
		assert(library->size() == 3 && library->indexOf("wood") == 1u && !library->indexOf("glass").has_value());
		assert(library->library().materials[0].toughness == 500.0f && library->info(0).tag == "stone");
		assert(library->info(0).dust_colour[2] == 160);

		const PhysicalMaterial& wood = library->library().materials[1];
		assert(wood.isFlammable() && wood.static_friction == 0.7f && wood.restitution == PhysicalMaterial {}.restitution);
		assert(library->library().materials[2].isIndestructible());

		const auto again = VoxelMaterialLibrary::fromToml(reparse(library->serialize(SaveMode::editor)));
		assert(again->serialize(SaveMode::editor) == library->serialize(SaveMode::editor));
		assert(again->library().materials[1].static_friction == 0.7f && again->info(0) == library->info(0));
	}

	for (std::string_view text :
	     {"", "[[materials]]\ndensity = 10\n", "[[materials]]\nname = \"a\"\n",
	      "[[materials]]\nname = \"a\"\ndensity = 1\n[[materials]]\nname = \"a\"\ndensity = 2\n"}) {
		assert(rejects<VoxelMaterialLibrary>(text));
	}
	for (std::string_view field :
	     {"density = 70000", "density = \"heavy\"", "density = 2.5", "density = 1\ntoughness = true", "density = 1\nflammable = 1",
	      "density = 1\ndust_colour = [1, 2]", "density = 1\ndust_colour = [1, 2, 300]", "density = 1\nimpact_sound = \"nope\""}) {
		assert(rejects<VoxelMaterialLibrary>("[[materials]]\nname = \"a\"\n" + std::string(field) + "\n"));
	}

	const auto ghost = VoxelMaterialLibrary::fromToml(toml::parse("[[materials]]\nname = \"ghost\"\ndensity = 0\n"));
	assert(validateLibrary(ghost->library()).size() == 1);

	{
		const auto asset = VoxelPalette::fromToml(toml::parse(k_palette_text));
		const Palette& palette = asset->palette();
		assert(asset->libraryUid() == toast::UID::fromString("LMNOPQRSTUV") && palette.max_emissive == 64.0f);

		assert(palette.entries[1].albedo_g == 120 && palette.entries[1].roughness == 204 && palette.entries[1].material == 1);
		assert(palette.entries[7].reflectivity == 51 && palette.entries[7].emissive == 255 && palette.entries[7].transforms_to == 8);
		assert((palette.entries[7].flags & k_entry_transparent) != 0);
		assert(palette.entries[9].material == k_default_material && asset->defaultedEntries() == std::vector<uint8_t> {9});
		assert(palette.entries[0] == PaletteEntry {} && palette.entries[100] == PaletteEntry {});
		assert(validatePalette(palette, library->library()).empty());

		const auto again = VoxelPalette::fromToml(reparse(asset->serialize(SaveMode::editor)));
		assert(again->serialize(SaveMode::editor) == asset->serialize(SaveMode::editor));
		assert(again->palette().entries == palette.entries && again->defaultedEntries() == asset->defaultedEntries());
	}

	assert(VoxelPalette::fromToml(toml::parse("max_emissive = 1.0\n"))->libraryUid() == 0);
	for (std::string_view text :
	     {"[[entries]]\nindex = 0\n", "[[entries]]\nindex = 256\n", "[[entries]]\nindex = true\n", "[[entries]]\nalbedo = [1, 2, 3]\n",
	      "[[entries]]\nindex = 4\n[[entries]]\nindex = 4\n", "max_emissive = -1.0\n", "library = \"bad\"\n"}) {
		assert(rejects<VoxelPalette>(text));
	}
	for (std::string_view field :
	     {"albedo = [1, 2]", "albedo = [1, 2, 256]", "albedo = [true, 1, 1]", "roughness = 1.5", "roughness = -0.1", "roughness = true",
	      "material = 300", "transforms_to = -1", "transparent = 1"}) {
		assert(rejects<VoxelPalette>("[[entries]]\nindex = 4\n" + std::string(field) + "\n"));
	}

	assets::AssetRegistry::init();
	assert(assets::AssetRegistry::createToml("voxel_palette", toml::parse(k_palette_text))->type() == "voxel_palette");
	assert(assets::AssetRegistry::createToml("voxel_material_library", toml::parse(k_library_text))->type() == "voxel_material_library");
}
