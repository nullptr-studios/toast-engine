#include "gltf_importer.hpp"

#include "gltf_importer.h"    // ffi

#define GLM_ENABLE_EXPERIMENTAL

#include "asset_manager.hpp"
#include "mesh.hpp"
#include "prefab.hpp"

#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <nlohmann/json.hpp>
#include <toast/uid.hpp>
#define TINYGLTF3_IMPLEMENTATION
#define TINYGLTF3_ENABLE_FS
#include <tiny_gltf_v3.h>
#include <toast/log.hpp>

using namespace tinygltf3;

namespace assets {
auto generateIntermediates(const std::filesystem::path& path) {
	tg3_parse_options options;
	tg3_error_stack errors;
	tg3_model model;

	tg3_parse_options_init(&options);
	options.images_as_is = 1;
	tg3_error_stack_init(&errors);

	std::string path_str = path.string();
	auto success = tg3_parse_file(&model, &errors, path_str.c_str(), path_str.size(), &options);

	if (success != TG3_OK) {
		for (size_t i = 0; i < errors.count; ++i) {
			TOAST_ERROR("AssetManager", "{}", errors.entries[i].message);
		}
		return;
	}

	// map each GLTF mesh index to the name of the first scene node that references it
	std::vector<std::string> mesh_node_name(model.meshes_count);
	for (size_t i = 0; i < model.nodes_count; i++) {
		const tg3_node& n = model.nodes[i];
		if (n.mesh != -1 && mesh_node_name[n.mesh].empty()) {
			std::string name(n.name.data, n.name.len);
			mesh_node_name[n.mesh] = name.empty() ? "node_" + std::to_string(i) : name;
		}
	}

	struct MeshFile {
		std::unique_ptr<Mesh> mesh;
		std::string file_name;
	};

	std::vector<MeshFile> mesh_files;
	std::vector<std::vector<int>> mesh_prim_to_file(model.meshes_count);
	int prim_counter = 0;

	for (size_t i = 0; i < model.meshes_count; ++i) {
		const auto& m = model.meshes[i];
		const std::string base_name = !mesh_node_name[i].empty() ? mesh_node_name[i] : std::string(m.name.data, m.name.len);

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

		mesh_prim_to_file[i].resize(m.primitives_count);

		for (uint32_t pi = 0; pi < m.primitives_count; ++pi) {
			const tg3_primitive& prim = m.primitives[pi];
			const bool is_triangles = prim.mode == -1 || prim.mode == 4;
			TOAST_ASSERT(is_triangles, "AssetManager", "Mesh primitive is not triangles");

			auto find_attr = [&](const char* attr_name) {
				for (uint32_t j = 0; j < prim.attributes_count; j++) {
					if (strcmp(prim.attributes[j].key.data, attr_name) == 0) {
						return prim.attributes[j].value;
					}
				}
				return -1;
			};

			int pos_idx = find_attr("POSITION");
			TOAST_ASSERT(pos_idx != -1, "AssetManager", "Mesh has no POSITION attribute");
			int norm_idx = find_attr("NORMAL");
			int uv_idx = find_attr("TEXCOORD_0");
			int tan_idx = find_attr("TANGENT");
			int col_idx = find_attr("COLOR_0");

			const uint32_t vertex_count = model.accessors[pos_idx].count;
			std::vector<toast::renderer::Vertex> vertices(vertex_count);

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
					v.tangent = t;    // w is handedness, store or discard as needed
				}
				if (col_data) {
					memcpy(&v.color, col_data + (j * get_stride(col_idx, sizeof(glm::vec3))), sizeof(glm::vec3));
				}
			}

			TOAST_ASSERT(prim.indices != -1, "AssetManager", "Mesh has no indices");
			const auto& idx_acc = model.accessors[prim.indices];
			const uint8_t* idx_data = accessor_bytes(prim.indices);
			std::vector<assets::Mesh::Index> indices(idx_acc.count);
			for (uint32_t j = 0; j < idx_acc.count; j++) {
				if (idx_acc.component_type == 5125) {
					indices[j] = reinterpret_cast<const uint32_t*>(idx_data)[j];
				} else if (idx_acc.component_type == 5123) {
					indices[j] = reinterpret_cast<const uint16_t*>(idx_data)[j];
				} else {
					indices[j] = idx_data[j];
				}
			}

			std::string file_name = (m.primitives_count == 1) ? base_name : base_name + "_" + std::to_string(prim_counter);

			mesh_files.push_back(
			    {.mesh = std::make_unique<Mesh>(std::string_view(file_name), std::move(vertices), std::move(indices)),
			     .file_name = std::move(file_name)}
			);
			mesh_prim_to_file[i][pi] = static_cast<int>(mesh_files.size()) - 1;
			++prim_counter;
		}
	}
	TOAST_TRACE("AssetManager", "Imported {} meshes", mesh_files.size());

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

		std::string name(img.name.data, img.name.len);
		if (name.empty()) {
			std::string uri(img.uri.data, img.uri.len);
			if (!uri.empty()) {
				name = std::filesystem::path(uri).stem().string();
			} else {
				name = "texture_" + std::to_string(i);
			}
		}

		TOAST_ASSERT(img.buffer_view != -1, "AssetManager", "Embedded image has no buffer view");
		const auto& img_bv = model.buffer_views[img.buffer_view];
		const auto& img_buf = model.buffers[img_bv.buffer];
		const uint8_t* img_raw = img_buf.data.data + img_bv.byte_offset;

		textures.push_back({
		  .data = std::vector<uint8_t>(img_raw, img_raw + img_bv.byte_length),
		  .name = std::move(name),
		  .format = std::string(img.mime_type.data, img.mime_type.len),
		});
	}
	TOAST_TRACE("AssetManager", "Imported {} textures", textures.size());

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
		material_table.insert("vertex", "NOT IMPLEMENTED");      // TODO: Replace with UID of default vertex shader
		material_table.insert("fragment", "NOT IMPLEMENTED");    // TODO: Replace with UID of default fragment shader

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
	TOAST_TRACE("AssetManager", "Imported {} materials", materials.size());

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
	TOAST_TRACE("AssetManager", "Imported {} cameras", cameras.size());

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
	TOAST_TRACE("AssetManager", "Imported {} lights", lights.size());

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
			const auto& gltf_mesh = model.meshes[node.mesh];
			const auto& prim_files = mesh_prim_to_file[node.mesh];

			if (gltf_mesh.primitives_count == 1) {
				n["type"] = "toast::MeshNode";
				n["params"]["mesh"] = mesh_files[prim_files[0]].file_name;
				const auto& prim = gltf_mesh.primitives[0];
				if (prim.material != -1) {    // TODO: Update field names when MeshNode fields are created
					n["params"]["material"] =
					    std::string(model.materials[prim.material].name.data, model.materials[prim.material].name.len);
				}
			} else {
				n["type"] = "toast::Node3D";
				n["children"] = nlohmann::json::array();
				for (uint32_t pi = 0; pi < gltf_mesh.primitives_count; ++pi) {
					const auto& prim = gltf_mesh.primitives[pi];
					const std::string& fname = mesh_files[prim_files[pi]].file_name;
					nlohmann::json child;
					child["name"] = fname;
					child["type"] = "toast::MeshNode";
					child["transform"]["pos"] = {0.0f, 0.0f, 0.0f};
					child["transform"]["rot"] = {0.0f, 0.0f, 0.0f, 1.0f};
					child["transform"]["scl"] = {1.0f, 1.0f, 1.0f};
					child["params"]["mesh"] = fname;
					if (prim.material != -1) {    // TODO: Update field names when MeshNode fields are created
						child["params"]["material"] =
						    std::string(model.materials[prim.material].name.data, model.materials[prim.material].name.len);
					}
					n["children"].push_back(std::move(child));
				}
			}
		} else if (node.camera != -1) {
			const auto& cam = cameras[node.camera];
			n["type"] = "toast::Camera";
			n["params"]["projection"] = cam.type;    // TODO: change this once the camera has been made
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
				n["type"] = "toast::DirectionalLight";
			} else if (light.type == "point") {
				n["type"] = "toast::PointLightNode";
			} else if (light.type == "spot") {
				n["type"] = "toast::SpotLightNode";
			}

			// TODO: Change this once the lights have been made
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
			n["type"] = "toast::Node3D";
		}

		if (node.children_count > 0) {
			if (!n.contains("children")) {
				n["children"] = nlohmann::json::array();
			}
			for (uint32_t i = 0; i < node.children_count; i++) {
				n["children"].push_back(walk_node(node.children[i]));
			}
		}

		return n;
	};

	for (size_t i = 0; i < model.scenes_count; i++) {
		const auto& scene = model.scenes[i];
		nlohmann::json scene_json;
		std::string name = std::string(scene.name.data, scene.name.len);
		if (name.empty()) {
			name = path.filename().stem();
		}
		scene_json["name"] = name;
		scene_json["children"] = nlohmann::json::array();

		for (uint32_t j = 0; j < scene.nodes_count; j++) {
			scene_json["children"].push_back(walk_node(scene.nodes[j]));
		}

		scenes.push_back(std::move(scene_json));
	}
	TOAST_TRACE("AssetManager", "Imported {} nodes", scenes.size());

	// Save files in cache://<name_without_extension>/
	std::string base_name = path.stem().string();
	std::filesystem::path cache_dir = AssetManager::get().getCachePath() / base_name;
	std::filesystem::create_directories(cache_dir);

	// Save meshes
	for (const auto& mf : mesh_files) {
		auto binary = mf.mesh->toBinary();
		std::filesystem::path out = cache_dir / (mf.file_name + ".tmesh");
		std::ofstream f(out, std::ios::binary);
		f.write(reinterpret_cast<const char*>(binary.data()), binary.size());
	}
	TOAST_TRACE("AssetManager", "Saved {} meshes", mesh_files.size());

	// Save textures
	for (const auto& tex : textures) {
		std::string_view ext = (tex.format == "image/jpeg") ? ".jpg" : ".png";
		std::filesystem::path out = cache_dir / (tex.name + std::string(ext));
		std::ofstream f(out, std::ios::binary);
		f.write(reinterpret_cast<const char*>(tex.data.data()), tex.data.size());
	}
	TOAST_TRACE("AssetManager", "Saved {} texture intermediates", textures.size());

	// Save materials
	for (size_t i = 0; i < materials.size(); ++i) {
		const auto& mat = materials[i];
		const auto* name_node = mat.get_as<std::string>("name");
		std::string name = (name_node && !name_node->get().empty()) ? name_node->get() : "material_" + std::to_string(i);
		std::filesystem::path out = cache_dir / (name + ".tmat");
		std::ofstream f(out);
		f << mat;
	}
	TOAST_TRACE("AssetManager", "Saved {} materials", materials.size());

	// Save scenes
	for (const auto& scene : scenes) {
		std::string name = scene["name"].get<std::string>();
		std::filesystem::path out = cache_dir / (name + ".json");
		std::ofstream f(out);
		f << scene.dump(2);
	}
	TOAST_TRACE("AssetManager", "Saved {} node intermediates", scenes.size());

	tg3_model_free(&model);
	tg3_error_stack_free(&errors);
}

static void jsonToTnode(const nlohmann::json& scene_json, const std::filesystem::path& out) {
	Prefab prefab;

	std::function<void(const nlohmann::json&, const std::string&)> walk = [&](const nlohmann::json& n,
	                                                                          const std::string& parent_uid_str) {
		Prefab::BasicNode basic;
		basic.name = n["name"].get<std::string>();
		basic.type = n.value("type", "toast::Node3D");
		if (basic.type == "Node") {
			basic.type = "toast::Node3D";
		}

		toast::UID uid = toast::UID::make();
		const std::string uid_str = uid.get();

		basic.fields.push_back({"m_uid", toast::FieldType::uid_t, false, uid});
		basic.fields.push_back({"m_name", toast::FieldType::string_t, false, basic.name});
		basic.fields.push_back({"m_enabled", toast::FieldType::bool_t, false, true});
		if (!parent_uid_str.empty()) {
			basic.fields.push_back({"m_parent", toast::FieldType::uid_t, false, toast::UID(toast::UID::fromString(parent_uid_str))});
		}

		if (n.contains("transform")) {
			Prefab::Group tg;
			tg.name = "Transform";
			const auto& t = n["transform"];
			const auto& p = t["pos"];
			const auto& r = t["rot"];
			const auto& s = t["scl"];
			tg.fields.push_back({
			  "m_position", toast::FieldType::vec3_t, false, glm::vec3 {p[0].get<float>(), p[1].get<float>(), p[2].get<float>()}
			});
			tg.fields.push_back({
			  "m_rotation",
			  toast::FieldType::quaternion_t,
			  false,
			  glm::quat {r[3].get<float>(), r[0].get<float>(), r[1].get<float>(), r[2].get<float>()}
			});
			tg.fields.push_back({
			  "m_scale", toast::FieldType::vec3_t, false, glm::vec3 {s[0].get<float>(), s[1].get<float>(), s[2].get<float>()}
			});
			basic.groups.push_back(std::move(tg));
		}

		if (basic.type == "toast::MeshNode" && n.contains("params")) {
			const auto& params = n["params"];
			// TODO: Update field names when MeshNode fields are created
			if (params.contains("mesh")) {
				const auto mesh_uid = params["mesh"].get<std::string>();
				if (mesh_uid.size() == 11) {
					basic.fields.push_back({"m_mesh", toast::FieldType::uid_t, false, toast::UID(toast::UID::fromString(mesh_uid))});
				} else {
					TOAST_WARN("AssetManager", "GLTF scene contains non-UID mesh reference '{}'; skipping m_mesh assignment", mesh_uid);
				}
			}
			if (params.contains("material")) {
				const auto material_uid = params["material"].get<std::string>();
				if (material_uid.size() == 11) {
					basic.fields.push_back(
					    {"m_material", toast::FieldType::uid_t, false, toast::UID(toast::UID::fromString(material_uid))}
					);
				} else {
					TOAST_WARN(
					    "AssetManager", "GLTF scene contains non-UID material reference '{}'; skipping m_material assignment", material_uid
					);
				}
			}
		}
		// TODO: Update field names when Camera/Light classes are created

		prefab.nodes.push_back(std::move(basic));

		if (n.contains("children")) {
			for (const auto& c : n["children"]) {
				walk(c, uid_str);
			}
		}
	};

	walk(scene_json, "");

	std::ofstream f(out);
	f << prefab.toFile();
}
}

extern "C" {

void gltf_generate_intermediates(const char* path) noexcept {
	std::filesystem::path dir {path};
	assets::generateIntermediates(dir);
}

void gltf_create_tnode(const char* json_path, const char* output_path) noexcept {
	std::ifstream f(json_path);
	assets::jsonToTnode(nlohmann::json::parse(f), std::filesystem::path(output_path));
}
}
