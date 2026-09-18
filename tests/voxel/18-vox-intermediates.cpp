#include "test_registry.hpp"
#include "vox_builder.hpp"
#include "voxel_test_utils.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <toast/assets/prefab.hpp>
#include <toast/assets/vox_intermediates.hpp>
#include <toast/assets/voxel_model.hpp>
#include <toast/assets/voxel_palette.hpp>
#include <toml++/toml.hpp>
#include <vector>

using namespace assets;
using voxel::LatticeOrientation;
using voxel::LatticePlacement;
using voxeltest::throws;

namespace {

struct ScratchDir {
	std::filesystem::path path =
	    std::filesystem::temp_directory_path() / ("toast_vox_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

	ScratchDir() { std::filesystem::create_directories(path); }

	~ScratchDir() {
		std::error_code ignored;
		std::filesystem::remove_all(path, ignored);
	}
};

void writeBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
	std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

[[nodiscard]]
auto readText(const std::filesystem::path& path) -> std::string {
	std::ifstream file(path);
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

[[nodiscard]]
auto readBytes(const std::filesystem::path& path) -> std::vector<uint8_t> {
	std::ifstream file(path, std::ios::binary);
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

[[nodiscard]]
auto placementOf(const Prefab::BasicNode& node) -> std::optional<LatticePlacement> {
	const glm::mat4 transform = glm::translate(glm::mat4(1.0f), node.find("position")->as<glm::vec3>()) *
	                            glm::mat4_cast(node.find("rotation")->as<glm::quat>()) *
	                            glm::scale(glm::mat4(1.0f), node.find("scale")->as<glm::vec3>());
	return voxel::placementFromTransform(transform);
}

}

TOAST_TEST_NAMED("voxel", "voxel/18-vox-intermediates", test_voxel_18_vox_intermediates) {
	LatticeOrientation mirrored;
	mirrored.flip = {true, false, false};

	voxbuild::Builder car;
	car.size(8, 8, 8);
	car.xyzi({{0, 0, 0, 1}, {7, 7, 7, 2}});
	car.size(4, 4, 4);
	car.xyzi({{0, 0, 0, 3}, {1, 1, 1, 3}});
	car.rgba();
	car.matl(1, {{"_rough", "0.5"}});
	car.ntrn(0, 1, {{"_name", "car"}}, {});
	car.ngrp(1, {2, 4, 6});
	car.ntrn(2, 3, {{"_name", "body"}}, {{"_t", "0 0 4"}});
	car.nshp(3, 0);
	car.ntrn(4, 5, {{"_name", "wheel_left"}}, {{"_t", "10 0 0"}});
	car.nshp(5, 1);
	car.ntrn(6, 7, {{"_name", "wheel_right"}, {"_hidden", "1"}}, {{"_t", "-10 0 0"}, {"_r", std::to_string(voxOrientationToByte(mirrored))}});
	car.nshp(7, 1);

	const ScratchDir scratch;
	const std::filesystem::path out = scratch.path / "cache";
	writeBytes(scratch.path / "car.vox", car.file(200));

	const uint64_t palette_uid = toast::UID::fromString("VoxPALETTE0");
	const VoxIntermediates written = writeVoxIntermediates(scratch.path / "car.vox", out, palette_uid);
	assert(written.base_name == "car" && written.palette_file_name == "car.tpal" && written.manifest_file_name == "car.json");
	assert(written.models.size() == 2 && written.warnings.empty());
	assert(written.models[0].file_name == "car_body.tvox" && written.models[1].file_name == "car_wheel_left.tvox");

	const VoxelModel body(readBytes(out / "car_body.tvox"));
	assert(body.brickDims() == glm::uvec3(1) && body.solidVoxelCount() == 2 && body.paletteUid() == palette_uid);

	const std::unique_ptr<VoxelPalette> palette = VoxelPalette::fromToml(toml::parse(readText(out / "car.tpal")));
	assert(palette->palette().entries[1].albedo_g == 255 && palette->palette().entries[1].roughness == 128);
	assert(palette->defaultedEntries().size() == 255 && palette->libraryUid() == 0);

	const nlohmann::json manifest = nlohmann::json::parse(readText(out / "car.json"));
	assert(manifest["palette"] == "car.tpal" && manifest["nodes"].size() == 1);
	assert(manifest["nodes"][0]["type"] == "toast::Node3D" && manifest["nodes"][0]["children"].size() == 3);
	assert(manifest["nodes"][0]["children"][2]["model"] == "car_wheel_left.tvox");

	{
		std::string text = readText(out / "car.json");
		const std::vector<std::pair<std::string, std::string>> uids {
		  {"car_body.tvox", "VoxMODEL000"}, {"car_wheel_left.tvox", "VoxMODEL001"}, {"car.tpal", "VoxPALETTE0"}
		};
		for (const auto& [from, to] : uids) {
			for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at)) {
				text.replace(at, from.size(), to);
			}
		}
		std::ofstream(out / "car.json", std::ios::trunc) << text;
	}
	voxManifestToPrefab(out / "car.json", out / "car.tnode");

	std::stringstream prefab_text(readText(out / "car.tnode"));
	const Prefab prefab(prefab_text);
	assert(prefab.nodes.size() == 4 && prefab.nodes[0].type == "toast::Node3D" && !prefab.nodes[0].find("m_model").has_value());

	const uint64_t root_uid = prefab.nodes[0].find("m_uid")->as<toast::UID>().data();
	for (size_t i = 1; i < prefab.nodes.size(); ++i) {
		const Prefab::BasicNode& node = prefab.nodes[i];
		assert(node.type == "toast::VoxelNode" && node.find("m_parent")->as<toast::UID>().data() == root_uid);
		assert(node.find("m_palette")->as<toast::UID>().data() == palette_uid && node.find("m_mobility")->as<int>() == 0);
		assert(node.find("m_local_enabled")->as<bool>() == (i != 3));
	}
	assert(prefab.nodes[3].find("m_model")->as<toast::UID>().data() == toast::UID::fromString("VoxMODEL001"));

	assert((placementOf(prefab.nodes[1]) == LatticePlacement {LatticeOrientation {}, glm::ivec3(-4, -4, 0)}));
	assert((placementOf(prefab.nodes[2]) == LatticePlacement {LatticeOrientation {}, glm::ivec3(8, -2, -2)}));
	assert((placementOf(prefab.nodes[3]) == LatticePlacement {mirrored, glm::ivec3(-8, -2, -2)}));

	{
		writeBytes(scratch.path / "rock.vox", voxbuild::Builder::singleVoxel(5, 5, 5).file());
		const VoxIntermediates rock = writeVoxIntermediates(scratch.path / "rock.vox", out, 0);
		assert(rock.models.size() == 1 && rock.models[0].file_name == "rock_model0.tvox");
		assert(VoxelModel(readBytes(out / "rock_model0.tvox")).paletteUid() == 0);

		const nlohmann::json json = nlohmann::json::parse(readText(out / "rock.json"));
		assert(json["nodes"].size() == 1 && json["nodes"][0]["type"] == "toast::VoxelNode" && !json["nodes"][0].contains("children"));
	}

	{
		std::ofstream(out / "unpatched.json") << R"({"name":"x","palette":"x.tpal","nodes":[{"name":"n","type":"toast::VoxelNode","model":"x.tvox"}]})";
		voxManifestToPrefab(out / "unpatched.json", out / "unpatched.tnode");
		std::stringstream text(readText(out / "unpatched.tnode"));
		const Prefab unpatched(text);
		assert(unpatched.nodes.size() == 1 && !unpatched.nodes[0].find("m_model").has_value());
		assert(!unpatched.nodes[0].find("m_palette").has_value() && unpatched.nodes[0].find("m_mobility").has_value());
	}

	writeBytes(scratch.path / "junk.vox", {1, 2, 3, 4, 5, 6, 7, 8});
	assert(throws([&] { static_cast<void>(writeVoxIntermediates(scratch.path / "absent.vox", out, 0)); }));
	assert(throws([&] { static_cast<void>(writeVoxIntermediates(scratch.path / "junk.vox", out, 0)); }));
	assert(throws([&] { voxManifestToPrefab(out / "nothing.json", out / "nothing.tnode"); }));
}
