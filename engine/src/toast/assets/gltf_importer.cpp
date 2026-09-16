#include "gltf_importer.hpp"

#include "gltf_importer.h"    // ffi

#define GLM_ENABLE_EXPERIMENTAL

#include "animation.hpp"
#include "asset_manager.hpp"
#include "mesh.hpp"
#include "prefab.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <nlohmann/json.hpp>
#include <span>
#include <toast/uid.hpp>
#include <unordered_map>
#include <utility>
#define TINYGLTF3_IMPLEMENTATION
#define TINYGLTF3_ENABLE_FS
#include <tiny_gltf_v3.h>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>

using namespace tinygltf3;

namespace assets {

namespace {

/// @brief Decodes a standard (RFC 4648) base64 payload, e.g. the part of a data: URI after the comma
auto decodeBase64(std::string_view input) -> std::vector<uint8_t> {
	ZoneScoped;
	static constexpr std::string_view k_alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	std::array<int8_t, 256> lookup {};
	lookup.fill(-1);
	for (size_t i = 0; i < k_alphabet.size(); ++i) {
		lookup[static_cast<uint8_t>(k_alphabet[i])] = static_cast<int8_t>(i);
	}

	std::vector<uint8_t> out;
	out.reserve(input.size() / 4 * 3);

	int32_t val = 0;
	int32_t bits = -8;
	for (const char c : input) {
		const int8_t digit = lookup[static_cast<uint8_t>(c)];
		if (digit == -1) {
			continue;    // padding ('='), whitespace, or line breaks - just skip
		}
		val = (val << 6) + digit;
		bits += 6;
		if (bits >= 0) {
			out.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
			bits -= 8;
		}
	}
	return out;
}

/// @brief Percent-decodes a glTF URI (e.g. "%20" -> ' '), per the glTF spec's URI encoding requirement
auto percentDecode(std::string_view uri) -> std::string {
	std::string out;
	out.reserve(uri.size());
	for (size_t i = 0; i < uri.size(); ++i) {
		if (uri[i] == '%' && i + 2 < uri.size()) {
			out.push_back(static_cast<char>(std::stoi(std::string(uri.substr(i + 1, 2)), nullptr, 16)));
			i += 2;
		} else {
			out.push_back(uri[i]);
		}
	}
	return out;
}

/// @brief Sniffs the KTX2 magic off the bytes rather than trusting mimeType or extension
///
/// Exporters ship KTX2 inside a glTF with no declared mimeType, and re-running those through toktx is
/// wasteful and pointless
auto isKtx2(std::span<const uint8_t> data) -> bool {
	static constexpr std::array<uint8_t, 12> k_ktx2_magic {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
	return data.size() >= k_ktx2_magic.size() && std::equal(k_ktx2_magic.begin(), k_ktx2_magic.end(), data.begin());
}

/// @brief Undecoded image bytes, from wherever glTF put them: a GLB bufferView, a data: URI, or a file
///
/// @returns empty when none of the three produced data, so callers must check
auto loadImageBytes(const tg3_model& model, const tg3_image& img, const std::filesystem::path& base_dir, size_t index)
    -> std::vector<uint8_t> {
	ZoneScoped;
	if (img.buffer_view != -1) {
		const auto& bv = model.buffer_views[img.buffer_view];
		const auto& buf = model.buffers[bv.buffer];
		const uint8_t* raw = buf.data.data + bv.byte_offset;
		return {raw, raw + bv.byte_length};
	}

	const std::string uri(img.uri.data, img.uri.len);
	if (uri.empty()) {
		TOAST_ERROR("AssetManager", "Image {} has neither a bufferView nor a uri; skipping", index);
		return {};
	}

	if (uri.starts_with("data:")) {
		const auto comma = uri.find(',');
		if (comma == std::string::npos) {
			TOAST_ERROR("AssetManager", "Image {} has a malformed data URI; skipping", index);
			return {};
		}
		return decodeBase64(std::string_view(uri).substr(comma + 1));
	}

	const std::filesystem::path file_path = base_dir / percentDecode(uri);
	std::ifstream file(file_path, std::ios::binary | std::ios::ate);
	if (!file) {
		TOAST_ERROR("AssetManager", "Image {} references '{}', which could not be opened", index, file_path.string());
		return {};
	}

	const auto size = file.tellg();
	file.seekg(0, std::ios::beg);
	std::vector<uint8_t> data(static_cast<size_t>(size));
	file.read(reinterpret_cast<char*>(data.data()), size);
	return data;
}

/// @brief Looks up an integer field nested inside a named glTF extension object (e.g. KHR_texture_basisu's
/// "source"). tinygltf3 has no first-class struct field for this - extensions it doesn't specifically model
/// are only exposed as generic tg3_value trees off tg3_extras_ext::extensions
auto findExtensionInt(const tg3_extras_ext& ext, std::string_view ext_name, std::string_view key) -> int32_t {
	for (uint32_t i = 0; i < ext.extensions_count; ++i) {
		const auto& e = ext.extensions[i];
		if (std::string_view(e.name.data, e.name.len) != ext_name) {
			continue;
		}
		if (e.value.type != TG3_VALUE_OBJECT) {
			return -1;
		}
		for (uint32_t j = 0; j < e.value.object_count; ++j) {
			const auto& kv = e.value.object_data[j];
			if (kv.value.type == TG3_VALUE_INT && std::string_view(kv.key.data, kv.key.len) == key) {
				return static_cast<int32_t>(kv.value.int_val);
			}
		}
		return -1;
	}
	return -1;
}

}    // namespace

auto generateIntermediates(const std::filesystem::path& path) {
	ZoneScoped;
	const glm::mat4 gltf_y_up_to_engine_z_up = glm::rotate(glm::mat4(1.0F), glm::radians(90.0F), glm::vec3(1.0F, 0.0F, 0.0F));
	const glm::mat4 engine_z_up_to_gltf_y_up = glm::transpose(gltf_y_up_to_engine_z_up);

	// NOLINTBEGIN(modernize-return-braced-init-list)
	auto to_engine_space_vec3 = [&](const glm::vec3& v) -> glm::vec3 {
		return glm::vec3(gltf_y_up_to_engine_z_up * glm::vec4(v, 1.0F));
	};

	auto to_engine_space_dir3 = [&](const glm::vec3& v) -> glm::vec3 {
		return glm::vec3(gltf_y_up_to_engine_z_up * glm::vec4(v, 0.0F));
	};
	// NOLINTEND(modernize-return-braced-init-list)

	auto to_engine_space_mat4 = [&](const glm::mat4& m) -> glm::mat4 {
		return gltf_y_up_to_engine_z_up * m * engine_z_up_to_gltf_y_up;
	};

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

	// Disambiguates mesh base names so two distinct meshes can never collide on the same .tmesh filename
	std::unordered_map<std::string, int> mesh_name_counts;

	for (size_t i = 0; i < model.meshes_count; ++i) {
		const auto& m = model.meshes[i];
		std::string base_name = !mesh_node_name[i].empty() ? mesh_node_name[i] : std::string(m.name.data, m.name.len);
		if (base_name.empty()) {
			base_name = "mesh_" + std::to_string(i);
		}
		if (auto it = mesh_name_counts.find(base_name); it != mesh_name_counts.end()) {
			base_name = base_name + "_" + std::to_string(it->second);
			++it->second;
		} else {
			mesh_name_counts[base_name] = 1;
		}

		// A sparse accessor legitimately has buffer_view = -1, so this is not a malformed file. Indexing
		// buffer_views with it reads out of bounds and hands back a pointer the memcpys below then trust
		auto accessor_bytes = [&](int acc_idx) -> const uint8_t* {
			const auto& acc = model.accessors[acc_idx];
			if (acc.buffer_view < 0) {
				return nullptr;
			}
			const auto& bv = model.buffer_views[acc.buffer_view];
			const auto& buf = model.buffers[bv.buffer];
			return buf.data.data + bv.byte_offset + acc.byte_offset;
		};

		auto get_stride = [&](int acc_idx, size_t tight_size) -> size_t {
			const auto& acc = model.accessors[acc_idx];
			if (acc.buffer_view < 0) {
				return tight_size;
			}
			const auto& bv = model.buffer_views[acc.buffer_view];
			return bv.byte_stride != 0 ? bv.byte_stride : tight_size;
		};

		/**
		 * @brief Reads a VEC3 accessor into a flat array, honouring sparse storage
		 *
		 * Morph targets are why: one moving 300 of 12000 vertices has no buffer view at all, and reading it
		 * as dense crashed the importer on most models - exporters use sparse by default
		 */
		auto read_vec3_accessor = [&](int acc_idx) -> std::vector<glm::vec3> {
			const auto& acc = model.accessors[acc_idx];
			std::vector<glm::vec3> values(acc.count, glm::vec3(0.0F));

			// Dense base values, when the accessor has any. A sparse accessor may still have a buffer view -
			// then the sparse entries are edits layered on top of it, rather than on top of zeros
			if (const uint8_t* data = accessor_bytes(acc_idx); data != nullptr) {
				const size_t stride = get_stride(acc_idx, sizeof(glm::vec3));
				for (uint64_t j = 0; j < acc.count; ++j) {
					memcpy(&values[j], data + (j * stride), sizeof(glm::vec3));
				}
			}

			const bool has_sparse = acc.sparse.is_sparse != 0 && acc.sparse.count > 0 && acc.sparse.indices.buffer_view >= 0 &&
			                        acc.sparse.values.buffer_view >= 0;
			if (!has_sparse) {
				return values;
			}

			const auto& index_view = model.buffer_views[acc.sparse.indices.buffer_view];
			const uint8_t* index_data =
			    model.buffers[index_view.buffer].data.data + index_view.byte_offset + acc.sparse.indices.byte_offset;

			const auto& value_view = model.buffer_views[acc.sparse.values.buffer_view];
			const uint8_t* value_data =
			    model.buffers[value_view.buffer].data.data + value_view.byte_offset + acc.sparse.values.byte_offset;

			for (int32_t s = 0; s < acc.sparse.count; ++s) {
				uint32_t target_index = 0;
				switch (acc.sparse.indices.component_type) {
					case TG3_COMPONENT_TYPE_UNSIGNED_BYTE: target_index = index_data[s]; break;
					case TG3_COMPONENT_TYPE_UNSIGNED_SHORT: {
						uint16_t narrow = 0;
						memcpy(&narrow, index_data + (static_cast<size_t>(s) * sizeof(uint16_t)), sizeof(narrow));
						target_index = narrow;
						break;
					}
					default: {
						memcpy(&target_index, index_data + (static_cast<size_t>(s) * sizeof(uint32_t)), sizeof(target_index));
						break;
					}
				}

				// An index past the accessor's own count is malformed; skip it rather than writing out of bounds
				if (target_index >= values.size()) {
					continue;
				}
				memcpy(&values[target_index], value_data + (static_cast<size_t>(s) * sizeof(glm::vec3)), sizeof(glm::vec3));
			}

			return values;
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
			if (pos_idx == -1) {
				// TOAST_ASSERT is compiled out in Release and isn't guaranteed to halt in Debug either -
				// falling through would index model.accessors[-1] next, straight out-of-bounds
				TOAST_ERROR("AssetManager", "Mesh primitive {} of mesh {} has no POSITION attribute; aborting import", pi, i);
				return;
			}
			int norm_idx = find_attr("NORMAL");
			int uv_idx = find_attr("TEXCOORD_0");
			int tan_idx = find_attr("TANGENT");
			int col_idx = find_attr("COLOR_0");
			int joints_idx = find_attr("JOINTS_0");
			int weights_idx = find_attr("WEIGHTS_0");

			const uint32_t vertex_count = model.accessors[pos_idx].count;
			std::vector<renderer::Vertex> vertices(vertex_count);

			const uint8_t* pos_data = accessor_bytes(pos_idx);
			if (pos_data == nullptr) {
				// Sparse positions are legal glTF but vanishingly rare, and the rest of this loop assumes a
				// dense stride. Fail the primitive cleanly instead of reading from nothing
				TOAST_ERROR(
				    "AssetManager",
				    "Mesh primitive {} of mesh {} stores POSITION in a sparse accessor, which is unsupported; aborting import",
				    pi,
				    i
				);
				return;
			}
			const uint8_t* norm_data = norm_idx != -1 ? accessor_bytes(norm_idx) : nullptr;
			const uint8_t* uv_data = uv_idx != -1 ? accessor_bytes(uv_idx) : nullptr;
			const uint8_t* tan_data = tan_idx != -1 ? accessor_bytes(tan_idx) : nullptr;
			const uint8_t* col_data = col_idx != -1 ? accessor_bytes(col_idx) : nullptr;

			// COLOR_0
			uint32_t col_components = 0;
			size_t col_component_size = 0;
			bool col_normalized = false;
			if (col_data != nullptr) {
				const auto& col_acc = model.accessors[col_idx];
				if (col_acc.type == TG3_TYPE_VEC4) {
					col_components = 4u;
				} else if (col_acc.type == TG3_TYPE_VEC3) {
					col_components = 3u;
				} else {
					col_components = 0u;
				}
				switch (col_acc.component_type) {
					case TG3_COMPONENT_TYPE_FLOAT: col_component_size = 4; break;
					case TG3_COMPONENT_TYPE_UNSIGNED_BYTE: col_component_size = 1; break;
					case TG3_COMPONENT_TYPE_UNSIGNED_SHORT: col_component_size = 2; break;
					default: col_component_size = 0; break;
				}
				// An integer COLOR_0 is normalised by the spec whether or not the flag says so
				col_normalized = col_acc.component_type != TG3_COMPONENT_TYPE_FLOAT;

				if (col_components == 0 || col_component_size == 0) {
					TOAST_WARN(
					    "AssetManager",
					    "Mesh primitive {} of mesh {} stores COLOR_0 as an unsupported type/component ({}/{}); importing it "
					    "without vertex colours",
					    pi,
					    i,
					    col_acc.type,
					    col_acc.component_type
					);
					col_data = nullptr;
				}
			}

			const size_t col_stride = col_data != nullptr ? get_stride(col_idx, col_components * col_component_size) : 0;

			for (uint32_t j = 0; j < vertex_count; j++) {
				auto& v = vertices[j];
				memcpy(&v.position, pos_data + (j * get_stride(pos_idx, sizeof(glm::vec3))), sizeof(glm::vec3));
				v.position = to_engine_space_vec3(v.position);
				if (norm_data) {
					memcpy(&v.normal, norm_data + (j * get_stride(norm_idx, sizeof(glm::vec3))), sizeof(glm::vec3));
					v.normal = to_engine_space_dir3(v.normal);
				}
				if (uv_data) {
					memcpy(&v.uv, uv_data + (j * get_stride(uv_idx, sizeof(glm::vec2))), sizeof(glm::vec2));
				}
				if (tan_data) {
					glm::vec4 t;
					memcpy(&t, tan_data + (j * get_stride(tan_idx, sizeof(glm::vec4))), sizeof(glm::vec4));
					const glm::vec3 tangent_xyz = to_engine_space_dir3(glm::vec3(t.x, t.y, t.z));
					v.tangent = glm::vec4(tangent_xyz, t.w);    // w is handedness
				}
				if (col_data) {
					// Alpha is read and dropped: Vertex::color is rgb, and nothing downstream blends on it
					const uint8_t* element = col_data + (j * col_stride);
					for (uint32_t c = 0; c < 3; ++c) {
						const uint8_t* component = element + (c * col_component_size);
						float value = 0.0f;
						if (!col_normalized) {
							memcpy(&value, component, sizeof(float));
						} else if (col_component_size == 1) {
							value = static_cast<float>(*component) / 255.0f;
						} else {
							uint16_t raw = 0;
							memcpy(&raw, component, sizeof(raw));
							value = static_cast<float>(raw) / 65535.0f;
						}
						v.color[static_cast<glm::length_t>(c)] = value;
					}
				}
			}

			// Skinning influences, kept in their own stream so static meshes don't carry the extra 24 bytes
			// per vertex. Both attributes are required together - weights without joints (or the reverse)
			// can't be applied to anything
			std::vector<renderer::SkinVertex> skin_vertices;
			if (joints_idx != -1 && weights_idx != -1) {
				const auto& joints_acc = model.accessors[joints_idx];
				const auto& weights_acc = model.accessors[weights_idx];

				// glTF stores joint indices as unsigned byte or short, and weights as float or a normalised
				// integer. Anything else is out of spec; skip rather than reinterpret the bytes wrongly
				const bool joints_ok = joints_acc.component_type == TG3_COMPONENT_TYPE_UNSIGNED_BYTE ||
				                       joints_acc.component_type == TG3_COMPONENT_TYPE_UNSIGNED_SHORT;
				const bool weights_ok = weights_acc.component_type == TG3_COMPONENT_TYPE_FLOAT;

				if (!joints_ok || !weights_ok) {
					TOAST_WARN(
					    "AssetManager",
					    "Mesh primitive {} of mesh {} has unsupported skinning component types (joints={}, weights={}); importing "
					    "it as a static mesh",
					    pi,
					    i,
					    joints_acc.component_type,
					    weights_acc.component_type
					);
				} else {
					const uint8_t* joints_data = accessor_bytes(joints_idx);
					const uint8_t* weights_data = accessor_bytes(weights_idx);
					const size_t joint_element = joints_acc.component_type == TG3_COMPONENT_TYPE_UNSIGNED_BYTE ? 1 : 2;
					const size_t joints_stride = get_stride(joints_idx, joint_element * 4);
					const size_t weights_stride = get_stride(weights_idx, sizeof(glm::vec4));

					skin_vertices.resize(vertex_count);
					for (uint32_t j = 0; j < vertex_count; ++j) {
						const uint8_t* joint_entry = joints_data + (j * joints_stride);
						for (int k = 0; k < 4; ++k) {
							if (joint_element == 1) {
								skin_vertices[j].joints[k] = joint_entry[k];
							} else {
								uint16_t joint = 0;
								memcpy(&joint, joint_entry + (k * 2), sizeof(joint));
								skin_vertices[j].joints[k] = joint;
							}
						}

						glm::vec4 w {};
						memcpy(&w, weights_data + (j * weights_stride), sizeof(glm::vec4));
						// Renormalise: exporters routinely emit weights summing to slightly off 1, and the
						// shader blends without normalising, so the error would show as subtle shrinking
						const float sum = w.x + w.y + w.z + w.w;
						skin_vertices[j].weights = sum > 0.0f ? w / sum : glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
					}
				}
			} else if (joints_idx != -1 || weights_idx != -1) {
				TOAST_WARN(
				    "AssetManager",
				    "Mesh primitive {} of mesh {} has only one of JOINTS_0/WEIGHTS_0; importing it as a static mesh",
				    pi,
				    i
				);
			}

			if (prim.indices == -1) {
				// Non-indexed primitives are technically valid glTF, but this importer doesn't support them
				// (no triangulation-on-the-fly path) - fail cleanly instead of indexing model.accessors[-1]
				TOAST_ERROR("AssetManager", "Mesh primitive {} of mesh {} has no indices; aborting import", pi, i);
				return;
			}
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

			// Needs the index buffer, so it can't happen in the vertex loop above
			if (tan_data == nullptr) {
				assets::generateTangents(vertices, indices);
				TOAST_TRACE("AssetManager", "Mesh primitive {} of mesh {} has no TANGENT attribute; generated tangents from UVs", pi, i);
			}

			std::string file_name = (m.primitives_count == 1) ? base_name : base_name + "_" + std::to_string(prim_counter);

			std::unique_ptr<Mesh> mesh_asset;
			if (!skin_vertices.empty()) {
				mesh_asset = std::make_unique<Mesh>(
				    std::string_view(file_name), std::move(vertices), std::move(indices), std::move(skin_vertices)
				);
			} else {
				mesh_asset = std::make_unique<Mesh>(std::string_view(file_name), std::move(vertices), std::move(indices));
			}

			mesh_files.push_back({.mesh = std::move(mesh_asset), .file_name = std::move(file_name)});
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

	// Index-aligned with model.textures (resized, not reserved+push_back'd) - tex_uid() below indexes this
	// by the raw glTF texture index, so a failed/skipped image must still leave a (empty) slot behind
	// rather than shifting every later texture's index down by one
	std::vector<TextureData> textures(model.textures_count);

	// Disambiguates texture names so two distinct images never collide on disk - a silent overwrite would
	// leave whichever material referenced the earlier one pointing at the wrong image
	std::unordered_map<std::string, int> texture_name_counts;

	for (size_t i = 0; i < model.textures_count; i++) {
		const auto& texture = model.textures[i];

		// KHR_texture_basisu points at its image through a nested extension object, not "source", and
		// tinygltf3 gives that no dedicated field - so every such texture reads source == -1 and is skipped
		// even though the image is right there
		const int32_t source = texture.source != -1 ? texture.source : findExtensionInt(texture.ext, "KHR_texture_basisu", "source");
		if (source == -1) {
			// TOAST_ASSERT is compiled out in Release and isn't guaranteed to halt in Debug either - this
			// must be a real early-out, not just a diagnostic, since indexing model.images[-1] next would be
			// straight out-of-bounds
			TOAST_ERROR("AssetManager", "Texture {} has no image source; skipping", i);
			continue;
		}
		const auto& img = model.images[source];

		std::string name(img.name.data, img.name.len);
		if (name.empty()) {
			std::string uri(img.uri.data, img.uri.len);
			if (!uri.empty()) {
				name = std::filesystem::path(uri).stem().string();
			} else {
				name = "texture_" + std::to_string(i);
			}
		}
		if (auto it = texture_name_counts.find(name); it != texture_name_counts.end()) {
			name = name + "_" + std::to_string(it->second);
			++it->second;
		} else {
			texture_name_counts[name] = 1;
		}

		auto data = loadImageBytes(model, img, path.parent_path(), i);

		std::string format;
		if (isKtx2(data)) {
			// Already compressed - skip straight past mimeType/extension guessing entirely so this never
			// gets routed through a PNG/JPEG path (and, downstream, a wasted re-run through toktx)
			format = "image/ktx2";
		} else {
			format = std::string(img.mime_type.data, img.mime_type.len);
			if (format.empty()) {
				// External file references commonly omit mimeType - the extension is the only signal left
				const std::string uri(img.uri.data, img.uri.len);
				const std::string ext = std::filesystem::path(uri).extension().string();
				format = (ext == ".jpg" || ext == ".jpeg") ? "image/jpeg" : "image/png";
			}
		}

		textures[i] = {
		  .data = std::move(data),
		  .name = std::move(name),
		  .format = std::move(format),
		};
	}
	TOAST_TRACE("AssetManager", "Imported {} textures", textures.size());

	// Materials
	std::vector<toml::table> materials;
	materials.reserve(model.materials_count);

	// One source of truth for the .tmat filename and every MeshNode's "material" reference. Referencing the
	// raw glTF name instead diverges whenever a material is unnamed or two share a name, and the reference
	// then matches no UID during the C# patch step and ends up unset
	std::unordered_map<std::string, int> material_name_counts;
	std::vector<std::string> material_file_names(model.materials_count);
	for (size_t i = 0; i < model.materials_count; i++) {
		std::string name(model.materials[i].name.data, model.materials[i].name.len);
		if (name.empty()) {
			name = "material_" + std::to_string(i);
		}
		if (auto it = material_name_counts.find(name); it != material_name_counts.end()) {
			name = name + "_" + std::to_string(it->second);
			++it->second;
		} else {
			material_name_counts[name] = 1;
		}
		material_file_names[i] = name;
	}

	// UID of the engine-shipped mesh.slang shader (engine/assets/shaders/mesh.slang.meta) - every imported
	// material renders through it, matching its reflected parameter set (tint/metallic/roughness factors,
	// gAlbedo/gNormal/gMetallicMap/gRoughnessMap samplers) exactly, see MaterialRuntime::resolveMemberValue()
	constexpr std::string_view k_mesh_shader_uid = "MdYTGGc3iHc";

	// Matches mesh.slang's per-sampler [Reflect] bindings and what MaterialRuntime::samplerFor() reads.
	// tex_name is the raw glTF name; the C# patch step swaps it for a UID once the sidecar exists
	auto insert_texture_slot = [](toml::table& out, std::string_view key, const std::string& tex_name) {
		toml::table slot;
		slot.insert("texture", tex_name);
		slot.insert("repeat_u", "repeat");
		slot.insert("repeat_v", "repeat");
		slot.insert("min_filter", "linear");
		slot.insert("mag_filter", "linear");
		slot.insert("mipmap_mode", "linear");
		slot.insert("anisotropy", true);
		out.insert(key, std::move(slot));
	};

	size_t opaque_blend_overrides = 0;
	size_t backface_culled = 0;
	size_t alpha_cutouts = 0;

	// doubleSided is as unreliable as alphaMode: Bistro sets it on all 132 materials, so it carries no
	// information and taking it at face value kills back-face culling scene-wide. Only meaningful when it
	// varies across the file; otherwise fall back to the name, which is where exporters record it
	// ("Foliage_Leaves.DoubleSided" - a leaf card is one quad and loses half its faces to culling)
	bool double_sided_is_informative = false;
	for (size_t i = 1; i < model.materials_count; i++) {
		if (model.materials[i].double_sided != model.materials[0].double_sided) {
			double_sided_is_informative = true;
			break;
		}
	}

	for (size_t i = 0; i < model.materials_count; i++) {
		const auto& mat = model.materials[i];
		const auto& pbr = mat.pbr_metallic_roughness;

		auto tex_name = [&](int32_t tex_idx) -> std::string {
			if (tex_idx == -1) {
				return "";
			}
			return textures[tex_idx].name;
		};

		toml::table material_table;
		material_table.insert("name", material_file_names[i]);
		material_table.insert("shaders", toml::array {std::string(k_mesh_shader_uid)});
		material_table.insert(
		    "tint",
		    toml::array {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3]}
		);
		material_table.insert("metallic", pbr.metallic_factor);
		material_table.insert("roughness", pbr.roughness_factor);

		insert_texture_slot(material_table, "gAlbedo", tex_name(pbr.base_color_texture.index));
		insert_texture_slot(material_table, "gNormal", tex_name(mat.normal_texture.index));

		// One texture, G=roughness B=metallic, bound to both slots with a channel selector rather than
		// re-encoded into two images
		//
		// Not a subtle error to get wrong: metallicFactor defaults to 1 *because* the texture supplies the
		// variation, so dropping the texture and keeping the factor makes everything fully metallic - and a
		// fully metallic surface has no diffuse response, so it renders black
		const bool packed_mr = pbr.metallic_roughness_texture.index != -1;
		insert_texture_slot(material_table, "gMetallicMap", tex_name(pbr.metallic_roughness_texture.index));
		insert_texture_slot(material_table, "gRoughnessMap", tex_name(pbr.metallic_roughness_texture.index));
		material_table.insert("metallicChannel", packed_mr ? 2.0 : 0.0);
		material_table.insert("roughnessChannel", packed_mr ? 1.0 : 0.0);

		// glTF's occlusionTexture is red-channel only and its strength is a plain multiplier, which maps
		// one-to-one onto mesh.slang's gOcclusionMap/occlusionStrength. Strength only means anything with a
		// map bound, so it stays at 0 for materials that have none rather than darkening them on import
		insert_texture_slot(material_table, "gOcclusionMap", tex_name(mat.occlusion_texture.index));
		material_table.insert("occlusionChannel", 0.0);
		material_table.insert("occlusionStrength", mat.occlusion_texture.index != -1 ? mat.occlusion_texture.strength : 0.0);

		// glTF has no emissive intensity of its own outside KHR_materials_emissive_strength, so the factor
		// carries the whole thing and intensity is 1 whenever there's anything to emit
		const bool emissive = mat.emissive_factor[0] > 0.0 || mat.emissive_factor[1] > 0.0 || mat.emissive_factor[2] > 0.0 ||
		                      mat.emissive_texture.index != -1;
		insert_texture_slot(material_table, "gEmissiveMap", tex_name(mat.emissive_texture.index));
		material_table.insert("emissiveColor", toml::array {mat.emissive_factor[0], mat.emissive_factor[1], mat.emissive_factor[2]});
		material_table.insert("emissiveIntensity", emissive ? 1.0 : 0.0);

		toml::table settings;
		const bool double_sided =
		    double_sided_is_informative ? mat.double_sided != 0 : material_file_names[i].contains("DoubleSided");
		if (!double_sided) {
			++backface_culled;
		}
		settings.insert("cull_mode", double_sided ? "none" : "back");

		// Exporters over-report alphaMode: Bistro marks all 132 materials BLEND despite every one having an
		// opaque baseColorFactor. Believing it sets depth_write=false scene-wide, which degenerates into
		// "whichever pass recorded last wins" - interiors draw over walls, back faces over front
		//
		// An opaque BLEND material renders identically to an opaque one, so require actual partial alpha
		const std::string alpha_mode(mat.alpha_mode.data, mat.alpha_mode.len);
		const bool blended = alpha_mode == "BLEND" && pbr.base_color_factor[3] < 1.0;
		if (alpha_mode == "BLEND" && !blended) {
			++opaque_blend_overrides;
		}

		settings.insert("blend_mode", blended ? "alpha" : "opaque");
		settings.insert("depth_test", true);
		settings.insert("depth_write", !blended);
		material_table.insert("settings", std::move(settings));

		// Order-independent and still depth-writing, so unlike blending it is safe to enable on suspicion.
		// MASK says so outright; otherwise fall back to the same naming cull_mode uses, since a card named
		// double-sided is a leaf card whose shape lives in its albedo alpha. Off by default, and reflected,
		// so it can be dialled in per material without a reimport
		const bool name_suggests_cutout = double_sided || material_file_names[i].contains("MASK");
		const bool cutout = alpha_mode == "MASK" || (!double_sided_is_informative && name_suggests_cutout);
		if (cutout) {
			++alpha_cutouts;
		}
		// glTF's own default when alphaMode is MASK but alphaCutoff is omitted
		double alpha_cutoff_value = 0.0;
		if (cutout) {
			alpha_cutoff_value = mat.alpha_cutoff > 0.0 ? mat.alpha_cutoff : 0.5;
		}
		material_table.insert("alphaCutoff", alpha_cutoff_value);

		materials.push_back(std::move(material_table));
	}

	if (opaque_blend_overrides > 0) {
		TOAST_WARN(
		    "AssetManager",
		    "{} of {} materials declared alphaMode=BLEND with a fully opaque baseColorFactor; imported as opaque so they still "
		    "write depth (see the blend_mode note in gltf_importer.cpp)",
		    opaque_blend_overrides,
		    materials.size()
		);
	}

	if (alpha_cutouts > 0) {
		TOAST_WARN(
		    "AssetManager", "alpha cutout enabled on {} of {} materials (albedo-alpha cut-out cards)", alpha_cutouts, materials.size()
		);
	}

	if (!double_sided_is_informative && model.materials_count > 1) {
		TOAST_WARN(
		    "AssetManager",
		    "every material reports doubleSided={}, so the flag was ignored as uninformative; back-face culling enabled on {} of "
		    "{} materials, the rest opted out via a 'DoubleSided' material name",
		    model.materials[0].double_sided != 0,
		    backface_culled,
		    materials.size()
		);
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

	// Converted into engine space here rather than at load time, so what lands on disk matches how mesh
	// vertices and node transforms were already handled above
	auto node_name_at = [&](int32_t node_idx) -> std::string {
		if (node_idx < 0 || std::cmp_greater_equal(node_idx, model.nodes_count)) {
			return {};
		}
		const auto& node = model.nodes[node_idx];
		std::string name(node.name.data, node.name.len);
		if (name.empty()) {
			name = "node_" + std::to_string(node_idx);
		}

		// applyFields() converts underscores to spaces for display, so the live tree never has one. Track
		// and joint names have to match *post*-conversion or resolveNode()'s find() never hits, so the same
		// conversion is mirrored here rather than threaded through applyFields()
		std::ranges::replace(name, '_', ' ');
		while (!name.empty() && name.back() == ' ') {
			name.pop_back();
		}
		return name;
	};

	// Reads an accessor as tightly-packed floats. Animation sampler inputs/outputs are always float in
	// practice (the spec allows normalised integer outputs, which this rejects rather than mis-decoding)
	auto read_float_accessor = [&](int32_t acc_idx, size_t components) -> std::vector<float> {
		if (acc_idx < 0 || std::cmp_greater_equal(acc_idx, model.accessors_count)) {
			return {};
		}
		const auto& acc = model.accessors[acc_idx];
		if (acc.component_type != TG3_COMPONENT_TYPE_FLOAT) {
			TOAST_WARN("AssetManager", "Animation accessor {} is not float-typed; skipping the track", acc_idx);
			return {};
		}
		if (acc.buffer_view < 0) {
			return {};
		}
		const auto& bv = model.buffer_views[acc.buffer_view];
		const auto& buf = model.buffers[bv.buffer];
		const uint8_t* base = buf.data.data + bv.byte_offset + acc.byte_offset;
		const size_t tight = components * sizeof(float);
		const size_t stride = bv.byte_stride != 0 ? bv.byte_stride : tight;

		std::vector<float> out(static_cast<size_t>(acc.count) * components);
		for (uint64_t i = 0; i < acc.count; ++i) {
			std::memcpy(out.data() + (i * components), base + (i * stride), tight);
		}
		return out;
	};

	std::vector<AnimationClip> clips;
	clips.reserve(model.animations_count);

	for (uint32_t a = 0; a < model.animations_count; ++a) {
		const auto& anim = model.animations[a];

		AnimationClip clip;
		clip.name = std::string(anim.name.data, anim.name.len);
		if (clip.name.empty()) {
			clip.name = "animation_" + std::to_string(a);
		}

		for (uint32_t c = 0; c < anim.channels_count; ++c) {
			const auto& channel = anim.channels[c];
			if (channel.sampler < 0 || std::cmp_greater_equal(channel.sampler, anim.samplers_count)) {
				continue;
			}
			const auto& sampler = anim.samplers[channel.sampler];
			const std::string path(channel.target.path.data, channel.target.path.len);

			AnimationTrack track;
			track.target_node = node_name_at(channel.target.node);
			if (track.target_node.empty()) {
				continue;
			}

			size_t components = 0;
			if (path == "translation") {
				track.target = TrackTarget::translation;
				components = 3;
			} else if (path == "rotation") {
				track.target = TrackTarget::rotation;
				components = 4;
			} else if (path == "scale") {
				track.target = TrackTarget::scale;
				components = 3;
			} else if (path == "weights") {
				// Morph targets are not supported; deformation is skeletal only. Skipped rather than failing
				// the import, since a rig that also ships blend shapes still animates correctly without them
				TOAST_WARN(
				    "AssetManager",
				    "Animation '{}' channel {} drives morph target weights, which this engine does not support; skipping channel",
				    clip.name,
				    c
				);
				continue;
			} else {
				TOAST_WARN("AssetManager", "Animation '{}' targets unsupported path '{}'; skipping channel", clip.name, path);
				continue;
			}

			const std::string interpolation(sampler.interpolation.data, sampler.interpolation.len);
			if (interpolation == "STEP") {
				track.interpolation = Interpolation::step;
			} else if (interpolation == "CUBICSPLINE") {
				track.interpolation = Interpolation::cubic_spline;
			} else {
				track.interpolation = Interpolation::linear;
			}

			track.times = read_float_accessor(sampler.input, 1);
			// `components` is the accessor element width: VEC3 for translation/scale, VEC4 for rotation
			const std::vector<float> values = read_float_accessor(sampler.output, components);
			if (track.times.empty() || values.empty()) {
				continue;
			}

			// CUBICSPLINE stores in-tangent/value/out-tangent per key, so the value array is 3x the times
			const size_t values_per_key = track.interpolation == Interpolation::cubic_spline ? 3 : 1;
			const size_t expected = track.times.size() * values_per_key * components;
			if (values.size() != expected) {
				TOAST_WARN(
				    "AssetManager",
				    "Animation '{}' channel {}: expected {} sampler output floats but found {}; skipping channel",
				    clip.name,
				    c,
				    expected,
				    values.size()
				);
				continue;
			}

			const size_t entries = values.size() / components;
			if (track.target == TrackTarget::rotation) {
				track.quat_values.reserve(entries);
				for (size_t i = 0; i < entries; ++i) {
					const float* v = values.data() + (i * 4);
					// glTF stores quaternions xyzw; glm::quat's constructor takes wxyz
					const glm::quat gltf_rotation(v[3], v[0], v[1], v[2]);
					track.quat_values.push_back(glm::quat_cast(to_engine_space_mat4(glm::mat4_cast(gltf_rotation))));
				}
			} else {
				track.vec3_values.reserve(entries);
				for (size_t i = 0; i < entries; ++i) {
					const float* v = values.data() + (i * 3);
					const glm::vec3 value(v[0], v[1], v[2]);
					if (track.target == TrackTarget::translation) {
						track.vec3_values.push_back(to_engine_space_vec3(value));
						continue;
					}

					const glm::mat4 converted = to_engine_space_mat4(glm::scale(glm::mat4(1.0F), value));
					track.vec3_values.emplace_back(converted[0][0], converted[1][1], converted[2][2]);
				}
			}

			clip.duration = std::max(clip.duration, track.times.empty() ? 0.0f : track.times.back());
			clip.tracks.push_back(std::move(track));
		}

		if (!clip.tracks.empty()) {
			clips.push_back(std::move(clip));
		}
	}

	// Skins
	std::vector<Skin> skins;
	skins.reserve(model.skins_count);

	for (uint32_t s = 0; s < model.skins_count; ++s) {
		const auto& gltf_skin = model.skins[s];

		Skin skin;
		skin.name = std::string(gltf_skin.name.data, gltf_skin.name.len);
		if (skin.name.empty()) {
			skin.name = "skin_" + std::to_string(s);
		}
		skin.skeleton_root = node_name_at(gltf_skin.skeleton);

		skin.joints.reserve(gltf_skin.joints_count);
		for (uint32_t j = 0; j < gltf_skin.joints_count; ++j) {
			skin.joints.push_back(node_name_at(gltf_skin.joints[j]));
		}

		if (gltf_skin.inverse_bind_matrices != -1) {
			const auto raw = read_float_accessor(gltf_skin.inverse_bind_matrices, 16);
			skin.inverse_bind_matrices.reserve(raw.size() / 16);
			for (size_t i = 0; i + 16 <= raw.size(); i += 16) {
				glm::mat4 m {};
				std::memcpy(&m, raw.data() + i, sizeof(glm::mat4));
				skin.inverse_bind_matrices.push_back(to_engine_space_mat4(m));
			}
		}

		skins.push_back(std::move(skin));
	}

	if (!clips.empty() || !skins.empty()) {
		size_t track_total = 0;
		for (const auto& clip : clips) {
			track_total += clip.tracks.size();
		}
		TOAST_TRACE(
		    "AssetManager", "Imported {} animation clip(s), {} track(s), {} skin(s)", clips.size(), track_total, skins.size()
		);
	}

	// One .tanim per clip, not per glTF: six clips in one file is one asset-browser entry, with nothing to
	// drag per animation. Each carries the skins too - small next to keyframe data, and it keeps every clip
	// able to pose the skeleton alone
	//
	// Named here rather than at save time because the scene JSON references them by bare filename
	const std::string animation_base = path.stem().string();
	std::vector<std::string> animation_file_names;
	animation_file_names.reserve(clips.size());
	{
		std::unordered_map<std::string, uint32_t> animation_name_counts;
		for (size_t i = 0; i < clips.size(); ++i) {
			// glTF clip names are free text ("Armature|Run Cycle.001"); anything outside this set would either
			// break the path or split the stem GltfImporter.cs keys its UID map by
			std::string sanitized;
			sanitized.reserve(clips[i].name.size());
			for (const char c : clips[i].name) {
				const bool keep = std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-';
				sanitized.push_back(keep ? c : '_');
			}
			if (sanitized.empty()) {
				sanitized = "clip_" + std::to_string(i);
			}

			std::string file_name = animation_base + "_" + sanitized;
			if (auto it = animation_name_counts.find(file_name); it != animation_name_counts.end()) {
				file_name = file_name + "_" + std::to_string(it->second);
				++it->second;
			} else {
				animation_name_counts[file_name] = 1;
			}
			// The suffixed name is registered too, so an authored clip literally called "Run_1" can't collide
			// with the deduped second "Run"
			animation_name_counts.emplace(file_name, 1);
			animation_file_names.push_back(std::move(file_name));
		}
	}

	// A glTF can define a skeleton with no animation at all, and MeshNode::skin_animation still needs a file
	// to point at - that case keeps the old single "<gltf>.tanim" holding skins and no clips
	const std::string skin_file_name = animation_file_names.empty() ? animation_base : animation_file_names.front();

	// Scenes
	std::vector<nlohmann::json> scenes;
	scenes.reserve(model.scenes_count);

	// glTF punctual lights carry physical units (candela / lux); anything this large is certainly not the
	// plain 0-1 multiplier m_intensity expects, and would wash the scene out without being obvious why
	constexpr double k_physical_intensity_threshold = 100.0;
	size_t physical_intensity_lights = 0;

	auto node_transform = [&](const tg3_node& node) -> nlohmann::json {
		nlohmann::json transform;
		glm::mat4 local_transform = glm::mat4(1.0F);

		if (node.has_matrix) {
			// double[16] into float[16], element by element. A memcpy copies the first 8 doubles' bit patterns
			// into 16 floats, and only files using a node "matrix" rather than TRS ever hit it. Both are
			// column-major, so index i maps straight across
			for (int i = 0; i < 16; ++i) {
				glm::value_ptr(local_transform)[i] = static_cast<float>(node.matrix[i]);
			}
		} else {
			const glm::vec3 t(node.translation[0], node.translation[1], node.translation[2]);
			const glm::quat r(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]);
			const glm::vec3 s(node.scale[0], node.scale[1], node.scale[2]);

			local_transform = glm::translate(glm::mat4(1.0F), t) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0F), s);
		}

		const glm::mat4 converted = to_engine_space_mat4(local_transform);
		// Initialised to identity rather than left to glm: decompose() returns false on a singular matrix
		// (a zero scale axis, say) and leaves every out-param untouched, so an unchecked call publishes
		// whatever was on the stack - which in a debug build is 0xCC filler, i.e. positions around -1e8
		glm::vec3 t {0.0F};
		glm::vec3 s {1.0F};
		glm::vec3 skew {0.0F};
		glm::vec4 persp {0.0F};
		glm::quat r {1.0F, 0.0F, 0.0F, 0.0F};
		if (!glm::decompose(converted, s, r, t, skew, persp)) {
			TOAST_WARN(
			    "AssetManager",
			    "Node '{}' has a non-decomposable transform; importing it with an identity transform",
			    std::string(node.name.data, node.name.len)
			);
			t = glm::vec3(0.0F);
			s = glm::vec3(1.0F);
			r = glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
		}

		transform["pos"] = {t.x, t.y, t.z};
		transform["rot"] = {r.x, r.y, r.z, r.w};
		transform["scl"] = {s.x, s.y, s.z};
		return transform;
	};

	std::function<nlohmann::json(int32_t)> walk_node = [&](int32_t node_idx) -> nlohmann::json {
		const tg3_node& node = model.nodes[node_idx];
		nlohmann::json n;
		// Must match node_name_at()'s "node_N" fallback exactly - animation tracks target nodes by this
		// same name (see track.target_node above), and glTF nodes are frequently unnamed. A raw empty name
		// here would leave AnimationPlayer::resolveNode() with nothing in the instantiated tree to find
		n["name"] = node_name_at(node_idx);
		n["transform"] = node_transform(node);

		if (node.mesh != -1) {
			const auto& gltf_mesh = model.meshes[node.mesh];
			const auto& prim_files = mesh_prim_to_file[node.mesh];

			// Skin params, shared by both the single- and multi-primitive branches below. glTF attaches the
			// skin to the node (not the mesh/primitive), so every primitive of a skinned mesh uses the same
			// skin - node.skin indexes model.skins, which parses 1:1 into `skins` earlier in this function
			const bool has_skin = node.skin != -1 && std::cmp_less(node.skin, skins.size());
			const auto apply_skin_params = [&](nlohmann::json& target) {
				if (!has_skin) {
					return;
				}
				// Bare filename, no extension, matching how "mesh"/"material" reference their targets and what
				// GltfImporter.cs keys animationUids by. Patched to a UID there
				target["params"]["skin_animation"] = skin_file_name;
				target["params"]["skin_name"] = skins[node.skin].name;
			};

			if (gltf_mesh.primitives_count == 1) {
				n["type"] = "toast::MeshNode";
				n["params"]["mesh"] = mesh_files[prim_files[0]].file_name;
				const auto& prim = gltf_mesh.primitives[0];
				if (prim.material != -1) {    // TODO: Update field names when MeshNode fields are created
					n["params"]["material"] = material_file_names[prim.material];
				}
				apply_skin_params(n);
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
						child["params"]["material"] = material_file_names[prim.material];
					}
					apply_skin_params(child);
					n["children"].push_back(std::move(child));
				}
			}
		} else if (node.camera != -1) {
			// Param keys below are the engine's reflected field names verbatim - jsonToTnode() emits them
			// straight into the prefab, and applyFields() silently skips anything NodeInfo doesn't know
			const auto& cam = cameras[node.camera];
			n["type"] = "toast::Camera";
			if (cam.type == "perspective") {
				// glTF yfov is radians; Camera::fov is degrees (getProjection() applies glm::radians itself)
				n["params"]["fov"] = glm::degrees(static_cast<float>(cam.yfov));
				n["params"]["near_plane"] = cam.znear;
				// zfar is optional in glTF (absent means infinite projection); 0 means it wasn't given
				if (cam.zfar > 0.0) {
					n["params"]["far_plane"] = cam.zfar;
				}
			} else {
				// Camera only does perspective - no ortho fields exist to write to, so keep its defaults
				// rather than inventing a mapping
				TOAST_WARN(
				    "AssetManager", "Camera '{}' is orthographic; toast::Camera is perspective-only, importing with defaults", cam.name
				);
			}
		} else if (node.light != -1) {
			const auto& light = lights[node.light];
			if (light.type == "directional") {
				n["type"] = "toast::DirectionalLight";
			} else if (light.type == "point") {
				n["type"] = "toast::PointLight";
			} else if (light.type == "spot") {
				n["type"] = "toast::Spotlight";
			} else {
				TOAST_WARN("AssetManager", "Light '{}' has unknown type '{}'; importing as a plain Node3D", light.name, light.type);
				n["type"] = "toast::Node3D";
			}

			if (n["type"] != "toast::Node3D") {
				n["params"]["m_light_color"] = {light.color[0], light.color[1], light.color[2]};
				n["params"]["m_intensity"] = light.intensity;

				// glTF intensity is physical - candela for point/spot, lux for directional - while
				// m_intensity is a plain multiplier defaulting to 1. Imported as-is rather than scaled by
				// some invented constant, but flagged so the values don't silently blow out the exposure
				if (light.intensity > k_physical_intensity_threshold) {
					++physical_intensity_lights;
				}

				// glTF range 0/absent means "infinite"; the engine has no such concept, so leave
				// m_attenuation at its own default instead of writing a 0 that would kill the light
				if (light.type != "directional" && light.range > 0.0) {
					n["params"]["m_attenuation"] = light.range;
				}

				if (light.type == "spot") {
					// glTF cone angles are radians, m_inner_radius/m_outer_radius are Unit("°")
					n["params"]["m_inner_radius"] = glm::degrees(static_cast<float>(light.inner_cone_angle));
					n["params"]["m_outer_radius"] = glm::degrees(static_cast<float>(light.outer_cone_angle));
				}
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
			name = path.filename().stem().string();
		}
		scene_json["name"] = name;
		scene_json["children"] = nlohmann::json::array();

		for (uint32_t j = 0; j < scene.nodes_count; j++) {
			scene_json["children"].push_back(walk_node(scene.nodes[j]));
		}

		// Has to be an ancestor of every joint, every skinned MeshNode and every animated node at once.
		// Wrapping the whole scene is the only placement that satisfies all three without knowing in advance
		// what the glTF targets
		if (!clips.empty() || !skins.empty()) {
			scene_json["type"] = "toast::AnimationPlayer";
			// The first clip's asset, since a player holds one animation at a time; the rest import alongside
			// it as their own assets for the user to swap in. Bare filename, see skin_animation above
			scene_json["params"]["animation"] = skin_file_name;
			if (!clips.empty()) {
				scene_json["params"]["clip"] = clips.front().name;
			}
		}

		scenes.push_back(std::move(scene_json));
	}
	TOAST_TRACE("AssetManager", "Imported {} nodes", scenes.size());

	if (physical_intensity_lights > 0) {
		TOAST_WARN(
		    "AssetManager",
		    "{} light(s) have a glTF intensity above {} (physical candela/lux units); imported unscaled into "
		    "m_intensity, which is a plain multiplier - expect to rescale them",
		    physical_intensity_lights,
		    k_physical_intensity_threshold
		);
	}

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
		if (tex.data.empty()) {
			continue;    // failed to load (already logged) - don't write a bogus empty file
		}
		std::string_view ext = ".png";
		if (tex.format == "image/jpeg") {
			ext = ".jpg";
		} else if (tex.format == "image/ktx2") {
			ext = ".ktx2";
		}
		std::filesystem::path out = cache_dir / (tex.name + std::string(ext));
		std::ofstream f(out, std::ios::binary);
		f.write(reinterpret_cast<const char*>(tex.data.data()), tex.data.size());
	}
	TOAST_TRACE("AssetManager", "Saved {} texture intermediates", textures.size());

	// Save materials
	for (size_t i = 0; i < materials.size(); ++i) {
		const std::filesystem::path out = cache_dir / (material_file_names[i] + ".tmat");
		std::ofstream f(out);
		f << materials[i];
	}
	TOAST_TRACE("AssetManager", "Saved {} materials", materials.size());

	// Save animations - one .tanim per clip, each with this glTF's skins alongside it (see
	// animation_file_names above for why)
	for (size_t i = 0; i < clips.size(); ++i) {
		const auto binary = Animation::toBinary({clips[i]}, skins);
		const std::filesystem::path out = cache_dir / (animation_file_names[i] + ".tanim");
		std::ofstream f(out, std::ios::binary);
		f.write(reinterpret_cast<const char*>(binary.data()), static_cast<std::streamsize>(binary.size()));
	}
	if (clips.empty() && !skins.empty()) {
		// Skeleton with no animation: still needs a file for MeshNode::skin_animation to resolve against
		const auto binary = Animation::toBinary({}, skins);
		const std::filesystem::path out = cache_dir / (skin_file_name + ".tanim");
		std::ofstream f(out, std::ios::binary);
		f.write(reinterpret_cast<const char*>(binary.data()), static_cast<std::streamsize>(binary.size()));
	}
	if (!clips.empty() || !skins.empty()) {
		TOAST_TRACE("AssetManager", "Saved {} animation asset(s), {} skin(s) each", std::max<size_t>(clips.size(), 1), skins.size());
	}

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
	ZoneScoped;
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
		// No m_name field here on purpose - Node's name isn't a reflected field; applyFields() sets it from
		// BasicNode::name (which basic.name above already carries)
		basic.fields.push_back({"m_local_enabled", toast::FieldType::bool_t, false, true});
		if (!parent_uid_str.empty()) {
			basic.fields.push_back({"m_parent", toast::FieldType::uid_t, false, toast::UID(toast::UID::fromString(parent_uid_str))});
		}

		if (n.contains("transform")) {
			Prefab::Group tg;
			tg.name = "Transform";
			const auto& t = n["transform"];
			auto p = t["pos"];
			auto r = t["rot"];
			auto s = t["scl"];

			// If for some reason the transform is null just initialize it to identity matrix
			if (p.is_null() || p[0].is_null() || p[1].is_null() || p[2].is_null()) {
				p = json_t::array({0.0, 0.0, 0.0});
			}

			if (r.is_null() || r[0].is_null() || r[1].is_null() || r[2].is_null() || r[3].is_null()) {
				r = json_t::array({0.0, 0.0, 0.0, 1.0});
			}

			if (s.is_null() || s[0].is_null() || s[1].is_null() || s[2].is_null()) {
				s = json_t::array({1.0, 1.0, 1.0});
			}

			// Must match Node3D's reflected field names exactly: applyFields() looks each up in the reflection
			// and skips anything missing without a diagnostic. The m_ prefix these once had silently zeroed
			// every imported node's transform
			tg.fields.push_back({
			  "position", toast::FieldType::vec3_t, false, glm::vec3 {p[0].get<float>(), p[1].get<float>(), p[2].get<float>()}
			});
			tg.fields.push_back({
			  "rotation",
			  toast::FieldType::quaternion_t,
			  false,
			  glm::quat {r[3].get<float>(), r[0].get<float>(), r[1].get<float>(), r[2].get<float>()}
			});
			tg.fields.push_back({
			  "scale", toast::FieldType::vec3_t, false, glm::vec3 {s[0].get<float>(), s[1].get<float>(), s[2].get<float>()}
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
			if (params.contains("skin_animation")) {
				const auto skin_uid = params["skin_animation"].get<std::string>();
				if (skin_uid.size() == 11) {
					basic.fields.push_back(
					    {"m_skin_animation", toast::FieldType::uid_t, false, toast::UID(toast::UID::fromString(skin_uid))}
					);
				} else {
					TOAST_WARN(
					    "AssetManager",
					    "GLTF scene contains non-UID skin_animation reference '{}'; skipping m_skin_animation assignment",
					    skin_uid
					);
				}
			}
			if (params.contains("skin_name")) {
				basic.fields.push_back({"m_skin_name", toast::FieldType::string_t, false, params["skin_name"].get<std::string>()});
			}
		}

		// AnimationPlayer: "animation" is the .tanim UID (patched by GltfImporter.cs like mesh/material
		// above), "clip" is a plain string naming which clip to autoplay
		else if (basic.type == "toast::AnimationPlayer" && n.contains("params")) {
			const auto& params = n["params"];
			if (params.contains("animation")) {
				const auto animation_uid = params["animation"].get<std::string>();
				if (animation_uid.size() == 11) {
					basic.fields.push_back(
					    {"m_animation", toast::FieldType::uid_t, false, toast::UID(toast::UID::fromString(animation_uid))}
					);
				} else {
					TOAST_WARN(
					    "AssetManager",
					    "GLTF scene contains non-UID animation reference '{}'; skipping m_animation assignment",
					    animation_uid
					);
				}
			}
			if (params.contains("clip")) {
				basic.fields.push_back({"m_clip_name", toast::FieldType::string_t, false, params["clip"].get<std::string>()});
			}
		}

		// Camera and light params. walk_node() already emits these keyed by the engine's reflected field
		// names, so they map straight across by type - the JSON value shape is what decides how to read it
		// (3-element array = vec3, anything else numeric = float)
		else if (basic.type != "toast::Node3D" && n.contains("params")) {
			for (const auto& [key, value] : n["params"].items()) {
				if (value.is_array() && value.size() == 3) {
					basic.fields.push_back({
					  key, toast::FieldType::vec3_t, false, glm::vec3 {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()}
					});
				} else if (value.is_number()) {
					basic.fields.push_back({key, toast::FieldType::float_t, false, value.get<float>()});
				} else {
					TOAST_WARN("AssetManager", "Unsupported param '{}' on node type '{}'; skipping", key, basic.type);
				}
			}
		}

		// toast::AmbientLight has no glTF equivalent (KHR_lights_punctual only covers
		// directional/point/spot), so it never appears here - nothing to map for it
		const bool is_light =
		    basic.type == "toast::DirectionalLight" || basic.type == "toast::PointLight" || basic.type == "toast::Spotlight";
		if (is_light && n.contains("params")) {
			const auto& params = n["params"];

			if (params.contains("color")) {
				const auto& c = params["color"];
				basic.fields.push_back({
				  "m_light_color", toast::FieldType::vec3_t, false, glm::vec3 {c[0].get<float>(), c[1].get<float>(), c[2].get<float>()}
				});
			}
			if (params.contains("intensity")) {
				// glTF intensity is physical (candela for point/spot, lux for directional); this engine's
				// m_intensity is a plain unitless shading multiplier with no such conversion - passed through
				// as-is since there's no established target scale to convert to yet
				basic.fields.push_back({"m_intensity", toast::FieldType::float_t, false, params["intensity"].get<float>()});
			}

			// PointLight/Spotlight only: glTF's range (0/absent = infinite) has no equivalent in this
			// engine's clustered-lighting model, which requires a finite culling radius - when range is
			// absent, leave m_attenuation at its class default rather than writing a bogus 0
			if ((basic.type == "toast::PointLight" || basic.type == "toast::Spotlight") && params.contains("range")) {
				basic.fields.push_back({"m_attenuation", toast::FieldType::float_t, false, params["range"].get<float>()});
			}

			if (basic.type == "toast::Spotlight") {
				if (params.contains("inner_cone_angle")) {
					basic.fields.push_back(
					    {"m_inner_radius", toast::FieldType::float_t, false, glm::degrees(params["inner_cone_angle"].get<float>())}
					);
				}
				if (params.contains("outer_cone_angle")) {
					basic.fields.push_back(
					    {"m_outer_radius", toast::FieldType::float_t, false, glm::degrees(params["outer_cone_angle"].get<float>())}
					);
				}
			}
		}

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
	// Both FFI entry points are noexcept, so anything thrown below - json, std::stoi on a bad percent-escape,
	// filesystem errors - would call std::terminate(). Logged instead, so a bad file fails diagnosably
	// rather than taking the editor with it
	try {
		std::filesystem::path dir {path};
		assets::generateIntermediates(dir);
	} catch (const std::exception& e) { TOAST_ERROR("AssetManager", "GLTF import failed: {}", e.what()); } catch (...) {
		TOAST_ERROR("AssetManager", "GLTF import failed with an unrecognized exception");
	}
}

void gltf_create_tnode(const char* json_path, const char* output_path) noexcept {
	try {
		std::ifstream f(json_path);
		assets::jsonToTnode(nlohmann::json::parse(f), std::filesystem::path(output_path));
	} catch (const std::exception& e) {
		TOAST_ERROR("AssetManager", "GLTF scene-to-tnode conversion failed: {}", e.what());
	} catch (...) { TOAST_ERROR("AssetManager", "GLTF scene-to-tnode conversion failed with an unrecognized exception"); }
}
}
