#include "gltf_importer.hpp"

#include "gltf_importer.h"    // ffi

#define GLM_ENABLE_EXPERIMENTAL

#include "asset_manager.hpp"
#include "mesh.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <nlohmann/json.hpp>
#include <tiny_gltf_v3.h>
#include <toast/log.hpp>

using namespace tinygltf3;

namespace assets {
auto generateIntermediates(const std::filesystem::path& path) {
	tg3_parse_options options;
	tg3_error_stack errors;
	tg3_model model;

	tg3_parse_options_init(&options);
	tg3_error_stack_init(&errors);

	std::string path_str = path.string();
	auto success = tg3_parse_file(&model, &errors, path_str.c_str(), path_str.size(), &options);

	if (success != TG3_OK) {
		for (size_t i = 0; i < errors.count; ++i) {
			TOAST_ERROR("AssetManager", "{}", errors.entries[i].message);
		}
		return;
	}

	// TODO: we need to save on cached://<filename>/ the following data
	//		1: all materials with a .tmat
	//		2: all textures as .png
	//		3: all meshes as .tmesh

	// Meshes
	std::vector<std::unique_ptr<Mesh>> meshes;
	meshes.reserve(model.meshes_count);

	for (size_t i = 0; i < model.meshes_count; ++i) {
		const auto& m = model.meshes[i];
		std::string_view name = {m.name.data, m.name.len};
		std::vector<assets::Mesh::Vertex> vertices;
		std::vector<assets::Mesh::Index> indices;

		TOAST_ASSERT(m.primitives_count == 1, "AssetManager", "Mesh has multiple primitives");
		const tg3_primitive& prim = m.primitives[0];
		const bool is_triangles = prim.mode == -1 || prim.mode == 4;
		TOAST_ASSERT(is_triangles, "AssetManager", "Mesh primitive is not triangles");

		auto find_attr = [&](const char* name) {
			for (uint32_t j = 0; j < prim.attributes_count; j++) {
				if (strcmp(prim.attributes[j].key.data, name) == 0) {
					return prim.attributes[j].value;
				}
			}
			return -1;
		};

		auto accessor_bytes = [&](int acc_idx) -> const uint8_t* {
			const auto& acc = model.accessors[acc_idx];
			const auto& bv = model.buffer_views[acc.buffer_view];
			const auto& buf = model.buffers[bv.buffer];
			return buf.data.data + bv.byte_offset + acc.byte_offset;
		};

		auto get_stride = [&](int acc_idx, size_t tight_size) {
			const auto& bv = model.buffer_views[model.accessors[acc_idx].buffer_view];
			return bv.byte_stride != 0 ? bv.byte_stride : tight_size;
		};

		int pos_idx = find_attr("POSITION");
		TOAST_ASSERT(pos_idx != -1, "AssetManager", "Mesh has no POSITION attribute");
		int norm_idx = find_attr("NORMAL");
		int uv_idx = find_attr("TEXCOORD_0");
		int tan_idx = find_attr("TANGENT");
		int col_idx = find_attr("COLOR_0");

		const uint32_t vertex_count = model.accessors[pos_idx].count;
		vertices.resize(vertex_count);

		const uint8_t* pos_data = accessor_bytes(pos_idx);
		const uint8_t* norm_data = norm_idx != -1 ? accessor_bytes(norm_idx) : nullptr;
		const uint8_t* uv_data = uv_idx != -1 ? accessor_bytes(uv_idx) : nullptr;
		const uint8_t* tan_data = tan_idx != -1 ? accessor_bytes(tan_idx) : nullptr;
		const uint8_t* col_data = col_idx != -1 ? accessor_bytes(col_idx) : nullptr;

		for (uint32_t j = 0; j < vertex_count; j++) {
			auto& v = vertices[j];
			memcpy(&v.position, pos_data + (j * get_stride(pos_idx, sizeof(glm::vec3))), sizeof(glm::vec3));
			if (norm_data) {
				memcpy(&v.normal, norm_data + (j * get_stride(norm_idx, sizeof(glm::vec3))), sizeof(glm::vec3));
			}
			if (uv_data) {
				memcpy(&v.uv, uv_data + (j * get_stride(uv_idx, sizeof(glm::vec2))), sizeof(glm::vec2));
			}
			if (tan_data) {
				glm::vec4 t;
				memcpy(&t, tan_data + (j * get_stride(tan_idx, sizeof(glm::vec4))), sizeof(glm::vec4));
				v.tangent = glm::vec3(t);    // w is handedness, store or discard as needed
			}
			if (col_data) {
				memcpy(&v.color, col_data + (j * get_stride(col_idx, sizeof(glm::vec3))), sizeof(glm::vec3));
			}
		}

		TOAST_ASSERT(prim.indices != -1, "AssetManager", "Mesh has no indices");
		const auto& idx_acc = model.accessors[prim.indices];
		const uint8_t* idx_data = accessor_bytes(prim.indices);
		indices.resize(idx_acc.count);
		for (uint32_t j = 0; j < idx_acc.count; j++) {
			if (idx_acc.component_type == 5125) {
				indices[j] = reinterpret_cast<const uint32_t*>(idx_data)[j];
			} else if (idx_acc.component_type == 5123) {
				indices[j] = reinterpret_cast<const uint16_t*>(idx_data)[j];
			} else {
				indices[j] = idx_data[j];
			}
		}

		meshes.emplace_back(new Mesh {name, std::move(vertices), std::move(indices)});
	}

	// Textures
	struct TextureData {
		std::vector<uint8_t> data;
		std::string name;
		std::string format;
	};

	std::vector<TextureData> textures;
	textures.reserve(model.textures_count);

	for (size_t i = 0; i < model.textures_count; i++) {
		const auto& texture = model.textures[i];
		TOAST_ASSERT(texture.source != -1, "AssetManager", "Texture has no image source");
		const auto& img = model.images[texture.source];
		TOAST_ASSERT(img.as_is, "AssetManager", "Image was decoded, expected raw bytes");

		std::string name(img.name.data, img.name.len);
		if (name.empty()) {
			std::string uri(img.uri.data, img.uri.len);
			if (!uri.empty()) {
				name = std::filesystem::path(uri).stem().string();
			} else {
				name = "texture_" + std::to_string(i);
			}
		}

		textures.push_back({
		  .data = std::vector<uint8_t>(img.image.data, img.image.data + img.image.count),
		  .name = std::move(name),
		  .format = std::string(img.mime_type.data, img.mime_type.len),
		});
	}

	// Materials
	std::vector<toml::table> materials;
	materials.reserve(model.materials_count);

	for (size_t i = 0; i < model.materials_count; i++) {
		const auto& mat = model.materials[i];
		const auto& pbr = mat.pbr_metallic_roughness;

		auto tex_uid = [&](int32_t tex_idx) -> std::string {
			if (tex_idx == -1) {
				return "";
			}
			return textures[tex_idx].name;
		};

		std::string mat_name(mat.name.data, mat.name.len);
		toml::table material_table;
		material_table.insert("uid", "");
		material_table.insert("name", mat_name);
		material_table.insert("vertex", "NOT IMPLEMENTED");
		material_table.insert("fragment", "NOT IMPLEMENTED");

		toml::table params;
		if (pbr.base_color_texture.index != -1) {
			params.insert("albedo_map", tex_uid(pbr.base_color_texture.index));
		}
		if (pbr.metallic_roughness_texture.index != -1) {
			params.insert("metallic_roughness_map", tex_uid(pbr.metallic_roughness_texture.index));
		}
		if (mat.normal_texture.index != -1) {
			params.insert("normal_map", tex_uid(mat.normal_texture.index));
			params.insert("normal_scale", mat.normal_texture.scale);
		}
		if (mat.occlusion_texture.index != -1) {
			params.insert("occlusion_map", tex_uid(mat.occlusion_texture.index));
			params.insert("occlusion_strength", mat.occlusion_texture.strength);
		}
		if (mat.emissive_texture.index != -1) {
			params.insert("emissive_map", tex_uid(mat.emissive_texture.index));
		}
		params.insert(
		    "base_color_factor",
		    toml::array {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3]}
		);
		params.insert("metallic_factor", pbr.metallic_factor);
		params.insert("roughness_factor", pbr.roughness_factor);
		params.insert("emissive_factor", toml::array {mat.emissive_factor[0], mat.emissive_factor[1], mat.emissive_factor[2]});
		material_table.insert("params", std::move(params));

		toml::table render_state;
		render_state.insert("cull_mode", mat.double_sided != 0 ? "none" : "back");
		render_state.insert("blend_mode", std::string(mat.alpha_mode.data, mat.alpha_mode.len));
		render_state.insert("depth_write", true);
		render_state.insert("alpha_cutoff", mat.alpha_cutoff);
		material_table.insert("render_state", std::move(render_state));

		materials.push_back(std::move(material_table));
	}

	// Cameras
	struct CameraData {
		std::string name;
		std::string type;    // "perspective" or "orthographic"
		// perspective
		double aspect_ratio;
		double yfov;
		double znear;
		double zfar;
		// orthographic
		double xmag;
		double ymag;
	};

	std::vector<CameraData> cameras;
	cameras.reserve(model.cameras_count);

	for (size_t i = 0; i < model.cameras_count; i++) {
		const auto& cam = model.cameras[i];
		std::string type(cam.type.data, cam.type.len);
		cameras.push_back({
		  .name = std::string(cam.name.data, cam.name.len),
		  .type = type,
		  .aspect_ratio = cam.perspective.aspect_ratio,
		  .yfov = cam.perspective.yfov,
		  .znear = cam.perspective.znear,
		  .zfar = cam.perspective.zfar,
		  .xmag = cam.orthographic.xmag,
		  .ymag = cam.orthographic.ymag,
		});
	}

	// Lights
	struct LightData {
		std::string name;
		std::string type;    // "directional", "point", "spot"
		glm::vec3 color;
		double intensity;
		double range;
		double inner_cone_angle;
		double outer_cone_angle;
	};

	std::vector<LightData> lights;
	lights.reserve(model.lights_count);

	for (size_t i = 0; i < model.lights_count; i++) {
		const auto& light = model.lights[i];
		lights.push_back({
		  .name = std::string(light.name.data, light.name.len),
		  .type = std::string(light.type.data, light.type.len),
		  .color = {light.color[0], light.color[1], light.color[2]},
		  .intensity = light.intensity,
		  .range = light.range,
		  .inner_cone_angle = light.spot.inner_cone_angle,
		  .outer_cone_angle = light.spot.outer_cone_angle,
		});
	}

	// Scenes
	std::vector<nlohmann::json> scenes;
	scenes.reserve(model.scenes_count);

	auto node_transform = [&](const tg3_node& node) -> nlohmann::json {
		nlohmann::json transform;
		if (node.has_matrix) {
			glm::mat4 mat;
			memcpy(&mat, node.matrix, sizeof(glm::mat4));
			glm::vec3 t, s, skew;
			glm::vec4 persp;
			glm::quat r;
			glm::decompose(mat, s, r, t, skew, persp);
			transform["pos"] = {t.x, t.y, t.z};
			transform["rot"] = {r.x, r.y, r.z, r.w};
			transform["scl"] = {s.x, s.y, s.z};
		} else {
			transform["pos"] = {node.translation[0], node.translation[1], node.translation[2]};
			transform["rot"] = {node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]};
			transform["scl"] = {node.scale[0], node.scale[1], node.scale[2]};
		}
		return transform;
	};

	std::function<nlohmann::json(int32_t)> walk_node = [&](int32_t node_idx) -> nlohmann::json {
		const tg3_node& node = model.nodes[node_idx];
		nlohmann::json n;
		n["name"] = std::string(node.name.data, node.name.len);
		n["transform"] = node_transform(node);

		if (node.mesh != -1) {
			n["type"] = "MeshNode";
			const auto& prim = model.meshes[node.mesh].primitives[0];
			n["params"]["mesh"] = std::string(model.meshes[node.mesh].name.data, model.meshes[node.mesh].name.len);
			if (prim.material != -1) {
				n["params"]["material"] = std::string(model.materials[prim.material].name.data, model.materials[prim.material].name.len);
			}
		} else if (node.camera != -1) {
			const auto& cam = cameras[node.camera];
			n["type"] = "CameraNode";
			n["params"]["projection"] = cam.type;
			if (cam.type == "perspective") {
				n["params"]["fov"] = cam.yfov;
				n["params"]["near"] = cam.znear;
				n["params"]["far"] = cam.zfar;
				n["params"]["aspect"] = cam.aspect_ratio;
			} else {
				n["params"]["xmag"] = cam.xmag;
				n["params"]["ymag"] = cam.ymag;
				n["params"]["near"] = cam.znear;
				n["params"]["far"] = cam.zfar;
			}
		} else if (node.light != -1) {
			const auto& light = lights[node.light];
			if (light.type == "directional") {
				n["type"] = "DirectionalLightNode";
			} else if (light.type == "point") {
				n["type"] = "PointLightNode";
			} else if (light.type == "spot") {
				n["type"] = "SpotLightNode";
			}

			n["params"]["color"] = {light.color[0], light.color[1], light.color[2]};
			n["params"]["intensity"] = light.intensity;
			if (light.range > 0.0) {
				n["params"]["range"] = light.range;
			}
			if (light.type == "spot") {
				n["params"]["inner_cone_angle"] = light.inner_cone_angle;
				n["params"]["outer_cone_angle"] = light.outer_cone_angle;
			}
		} else {
			n["type"] = "Node";
		}

		if (node.children_count > 0) {
			n["children"] = nlohmann::json::array();
			for (uint32_t i = 0; i < node.children_count; i++) {
				n["children"].push_back(walk_node(node.children[i]));
			}
		}

		return n;
	};

	for (size_t i = 0; i < model.scenes_count; i++) {
		const auto& scene = model.scenes[i];
		nlohmann::json scene_json;
		scene_json["name"] = std::string(scene.name.data, scene.name.len);
		scene_json["children"] = nlohmann::json::array();

		for (uint32_t j = 0; j < scene.nodes_count; j++) {
			scene_json["children"].push_back(walk_node(scene.nodes[j]));
		}

		scenes.push_back(std::move(scene_json));
	}

	// Save files in cache://<name_without_extension>/
	std::string base_name = path.stem().string();
	std::filesystem::path cache_dir = AssetManager::get().getCachePath() / base_name;
	std::filesystem::create_directories(cache_dir);

	// Save meshes
	for (const auto& mesh : meshes) {
		auto binary = mesh->toBinary();
		std::filesystem::path out = cache_dir / (mesh->name() + ".tmesh");
		std::ofstream f(out, std::ios::binary);
		f.write(reinterpret_cast<const char*>(binary.data()), binary.size());
	}

	// Save textures
	for (const auto& tex : textures) {
		std::filesystem::path out = cache_dir / (tex.name + ".tex");
		std::ofstream f(out, std::ios::binary);
		f.write(reinterpret_cast<const char*>(tex.data.data()), tex.data.size());
	}

	// Save materials
	for (const auto& mat : materials) {
		const auto& meta = *mat.get_as<toml::table>("material");
		std::string name = meta.get_as<std::string>("name")->get();
		std::filesystem::path out = cache_dir / (name + ".tmat");
		std::ofstream f(out);
		f << mat;
	}

	// Save scenes
	for (const auto& scene : scenes) {
		std::string name = scene["name"].get<std::string>();
		std::filesystem::path out = cache_dir / (name + ".json");
		std::ofstream f(out);
		f << scene.dump(2);
	}

	tg3_model_free(&model);
	tg3_error_stack_free(&errors);
}
}

extern "C" {

void gltf_generate_intermediates(const char* path) {
	std::filesystem::path dir {path};
	assets::generateIntermediates(dir);
}

}
