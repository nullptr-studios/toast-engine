#include "debug/voxel_debug.hpp"
#include "passes/debug/selection_outline.hpp"
#include "passes/geometry/voxel_pass.hpp"
#include "passes/lighting/shadow_pass.hpp"
#include "passes/lighting/traced_specular_pass.hpp"
#include "settings/renderer_settings.hpp"
#include "vulkan_renderer.hpp"

#include <SDL3/SDL_keycode.h>
#include <toast/assets/animation.hpp>
#include <toast/assets/asset_manager.hpp>
#include <toast/assets/material.hpp>
#include <toast/physics/simulator.hpp>
#include <toast/profiler_phases.hpp>
#include <toast/voxel/nodes/voxel_node.hpp>
#include <toast/voxel/runtime_pool.hpp>
#include <toast/voxel/volume_bounds.hpp>
#include <toast/voxel/voxel_constants.hpp>
#include <toast/window/window_events.hpp>
#include <toast/world/animation_player.hpp>
#include <toast/world/camera.hpp>
#include <toast/world/directional_light.hpp>
#include <toast/world/fog_volume.hpp>
#include <toast/world/irradiance_volume.hpp>
#include <toast/world/light.hpp>
#include <toast/world/light_gizmo.hpp>
#include <toast/world/mesh_node.hpp>
#include <toast/world/point_light.hpp>
#include <toast/world/post_process_volume.hpp>
#include <toast/world/reflection_probe.hpp>
#include <toast/world/spotlight.hpp>
#include <toast/world/workspace_events.hpp>

#if defined(_WIN32) && defined(TOAST_DEV_TOOLS)
#include <external/inc/nsight/NGFX_GPUTrace_Vulkan.h>
#include <external/inc/nsight/NGFX_GraphicsCapture_Vulkan.h>
#endif

#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace renderer {

namespace {

[[nodiscard]]
auto defaultVoxelPalette() -> const voxel::Palette& {
	return voxel::defaultPalette();
}

[[nodiscard]]
auto voxelFragmentRenderId(physics::ShapeID shape) -> uint64_t {
	return 0xF7A6'1D00'0000'0000ull ^ ((static_cast<uint64_t>(shape.slot) << 32) | shape.generation);
}

[[nodiscard]]
auto packedGlassBricks(const voxel::gpu::PackedScene& scene) -> std::vector<std::vector<uint32_t>> {
	std::vector<std::vector<uint32_t>> out(scene.records.size());
	for (uint32_t record = 0; record < scene.records.size(); ++record) {
		out[record] = voxel::gpu::glassBricks(scene, record);
	}
	return out;
}

auto brickCoord(const voxel::gpu::VolumeRecord& record, uint32_t index) -> uint32_t {
	const uint32_t x = index % record.brick_dims_x;
	const uint32_t y = (index / record.brick_dims_x) % record.brick_dims_y;
	const uint32_t z = index / (record.brick_dims_x * record.brick_dims_y);
	return x | (y << k_brick_coord_bits) | (z << (2 * k_brick_coord_bits));
}

auto hashBricks(const VoxelBrickList& list) -> uint64_t {
	uint64_t hash = 14695981039346656037ull;
	const auto mix = [&hash](uint32_t word) { hash = (hash ^ word) * 1099511628211ull; };
	for (const uint32_t brick : list.occupied) {
		mix(brick);
	}
	mix(0xFFFFFFFFu);
	for (const uint32_t brick : list.glass) {
		mix(brick);
	}
	mix(list.dims.x);
	mix(list.dims.y);
	mix(list.dims.z);
	return hash;
}

/// Records no delta touched share the list of @p previous
[[nodiscard]]
auto packedBrickLists(
    const voxel::gpu::PackedScene& scene, const std::vector<std::vector<uint32_t>>& glass, const VoxelGpuStorage& storage,
    const VoxelGpuStorage* previous, std::span<const VolumeDelta> deltas
) -> std::vector<std::shared_ptr<const VoxelBrickList>> {
	std::unordered_set<uint64_t> dirty;
	for (const VolumeDelta& delta : deltas) {
		if (delta.unknown || !delta.bricks.empty()) {
			dirty.insert(delta.node_rid);
		}
	}

	std::vector<std::shared_ptr<const VoxelBrickList>> out(scene.records.size());
	for (uint32_t r = 0; r < scene.records.size(); ++r) {
		const voxel::gpu::VolumeRecord& record = scene.records[r];
		auto list = std::make_shared<VoxelBrickList>();
		list->dims = glm::uvec3(record.brick_dims_x, record.brick_dims_y, record.brick_dims_z);
		for (const uint32_t index : glass[r]) {
			list->glass.push_back(brickCoord(record, index));
		}

		const uint64_t rid = storage.nodeRid(r);
		const auto kept = previous != nullptr && rid != 0 && !dirty.contains(rid) ? previous->recordIndexOf(rid) : std::nullopt;
		const VoxelBrickList* old = kept.has_value() ? previous->brickList(*kept).get() : nullptr;
		if (old != nullptr && old->dims != list->dims) {
			old = nullptr;
		}
		if (old != nullptr && old->glass == list->glass) {
			out[r] = previous->brickList(*kept);
			continue;
		}
		if (old != nullptr) {
			list->occupied = old->occupied;
		} else {
			const uint32_t count = record.brick_dims_x * record.brick_dims_y * record.brick_dims_z;
			for (uint32_t i = 0; i < count; ++i) {
				if (voxel::BrickEntry {scene.grids[record.grid_offset + i]}.tag() != voxel::BrickTag::empty) {
					list->occupied.push_back(brickCoord(record, i));
				}
			}
		}
		list->hash = hashBricks(*list);
		out[r] = std::move(list);
	}
	return out;
}

}

auto VulkanRenderer::voxelCollisionWorld() const -> std::shared_ptr<const VoxelCollisionWorld> {
	// i need a way of getting this on the physics thread without the engine exploding so just lock it for a sec
	// i don't think this is that bad of an idea but maybe it's like bad idk
	std::scoped_lock lock(m_voxel_collision_mutex);
	return m_voxel_collision_world;
}

void VulkanRenderer::buildVoxelProxies(RenderFrame& frame) {
	ZoneScoped;

	auto& nodes = m_tick_voxel_nodes;
	{
		std::scoped_lock lock(m_voxel_proxy_mutex);
		nodes.assign(m_voxel_proxy_nodes.begin(), m_voxel_proxy_nodes.end());
	}

	if (m_voxel_storage_pending) {
		if (m_voxel_storage_pending->isReady()) {
			m_voxel_storage = std::move(m_voxel_storage_pending);
			m_voxel_storage_pending.reset();
			m_voxel_change_tracker.published();
		} else if (m_voxel_storage_pending->hasFailed()) {
			if (!m_voxel_upload_failed_warned) {
				m_voxel_upload_failed_warned = true;
				TOAST_ERROR("Render", "Voxel scene upload failed; voxel volumes draw from the previous upload, if any");
			}
			m_voxel_storage_pending.reset();
			m_voxel_patcher.reset();
		}
	}

	struct Gathered {
		uint64_t id;
		const voxel::Volume* volume;
		const voxel::Palette* palette;
		glm::mat4 model;
		/// Node name and the suffix are only joined when a pack needs the debug rows
		std::string_view debug_name;
		std::string_view debug_suffix;
		uint32_t fragment_slot = ~0u;
		bool data_only = false;
		const toast::VoxelNode* node = nullptr;
		uint64_t outline_id = 0;
		bool piece_outlined = false;
		bool outline_only = false;
	};

	// chatty vibed this
	const auto part_render_id = [](uint64_t node_rid, uint64_t part_key) {
		uint64_t mixed = (node_rid * 0x9E3779B97F4A7C15ull) ^ ((part_key + 0x7F4A7C15ull) * 0xC2B2AE3D27D4EB4Full);
		mixed ^= mixed >> 29;
		return (mixed & 0x3FFFFFFFFFFFFFFFull) | (uint64_t {1} << 62);
	};

	std::vector<Gathered> gathered;
	std::vector<VoxelSceneKey> key;
	gathered.reserve(nodes.size());
	key.reserve(nodes.size());

	std::unique_lock voxel_lock {voxel::runtimePoolMutex()};

	for (auto* node : nodes) {
		if (node == nullptr || !node->enabled()) {
			continue;
		}
		static const glm::mat4 k_identity(1.0f);
		const glm::mat4* drawn_transform = ownerTransform(*node);
		const bool hidden = drawn_transform == nullptr;
		const glm::mat4* owner_transform = hidden ? &k_identity : drawn_transform;

		// volumes that doesn't ahve any voxels
		for (const toast::VoxelOutlineGhost& ghost : node->outlineGhosts()) {
			if (ghost.volume == nullptr) {
				continue;
			}
			const voxel::Palette* ghost_palette = node->resolvedPalette();
			if (ghost_palette == nullptr) {
				ghost_palette = &defaultVoxelPalette();
			}
			const uint64_t id = part_render_id(node->rid(), ghost.piece_rid ^ 0x6F75'746C'696E'6500ull);
			// clang-format off
			gathered.push_back(Gathered {
				.id = id,
				.volume = ghost.volume.get(),
				.palette = ghost_palette,
				.model = *owner_transform * node->getWorldTransform() * ghost.localTransform(),
				.debug_name = node->name(),
				.debug_suffix = " outline",
				.data_only = true,
				.node = node,
				.outline_only = !hidden
			});
			key.push_back({
				.node_rid = id,
				.revision = ghost.generation,
				.content = ghost.volume->revision(),
				.palette = ghost_palette,
				.palette_revision = ghost_palette->revision
			});
			// clang-format on
		}

		if (node->usesParts()) {
			const voxel::Palette* parts_palette = node->resolvedPalette();
			if (parts_palette == nullptr) {
				parts_palette = &defaultVoxelPalette();
			}
			for (const toast::VoxelPart& part : node->parts()) {
				if (part.volume == nullptr) {
					continue;
				}
				glm::mat4 pose = node->getWorldTransform();
				if (part.has_pose) {
					pose = glm::translate(glm::mat4(1.0f), part.pose_position) * glm::mat4_cast(part.pose_rotation);
				}
				const uint64_t id = part_render_id(node->rid(), part.key);
				// clang-format off
				gathered.push_back(Gathered {
					.id = id,
					.volume = part.volume.get(),
					.palette = parts_palette,
					.model = *owner_transform * physics::Simulator::renderPoseDelta(part) * pose * part.localTransform(),
					.debug_name = node->name(),
					.data_only = hidden,
					.node = node,
					.outline_id = node->rid(),
					.piece_outlined = std::ranges::any_of(part.outline_rids, [this](uint64_t rid) { return isOutlined(rid); })
				});
				key.push_back({
					.node_rid = id,
					.revision = part.revision,
					.content = part.volume->revision(),
					.palette = parts_palette,
					.palette_revision = parts_palette->revision
				});
				// clang-format on
			}
			continue;
		}

		voxel::Volume* volume = node->volume();
		if (volume == nullptr) {
			continue;
		}
		const voxel::Palette* palette = node->resolvedPalette();
		if (palette == nullptr) {
			palette = &defaultVoxelPalette();
		}

		// clang-format off
		gathered.push_back(Gathered {
			.id = node->rid(),
			.volume = volume,
			.palette = palette,
			.model = *owner_transform * physics::Simulator::renderPoseDelta(*node) * node->getWorldTransform() * node->volumeLocalTransform(),
			.debug_name = node->name(),
			.data_only = hidden,
			.node = node
		});
		key.push_back({
			.node_rid = node->rid(),
			.revision = node->revision(),
			.content = volume->revision(),
			.palette = palette,
			.palette_revision = palette->revision
		});
		// clang-format on
	}

	// Fragments of one blast share an origin and part entries repeat their node so each origin is checked once per node
	std::vector<std::pair<physics::BodyID, bool>> origin_owned;
	const auto owned_by_gathered = [&](physics::BodyID origin) {
		if (const auto known = std::ranges::find(origin_owned, origin, &std::pair<physics::BodyID, bool>::first);
		    known != origin_owned.end()) {
			return known->second;
		}
		bool owned = false;
		const toast::VoxelNode* checked = nullptr;
		for (const Gathered& g : gathered) {
			if (g.node == nullptr || g.data_only || g.node == checked) {
				continue;
			}
			checked = g.node;
			if (g.node->ownsBody(origin)) {
				owned = true;
				break;
			}
		}
		origin_owned.emplace_back(origin, owned);
		return owned;
	};

	for (const physics::VoxelRenderRecord& record : physics::Simulator::voxelFragmentRecords()) {
		if (record.volume == nullptr) {
			continue;
		}

		if (record.fragment_origin != physics::BodyID {} && !owned_by_gathered(record.fragment_origin)) {
			// do not leak voxel shit to other voxel shit
			continue;
		}

		const voxel::Palette* palette = record.palette != nullptr ? record.palette : &defaultVoxelPalette();
		const uint64_t id = voxelFragmentRenderId(record.shape);

		const glm::mat4 drawn_transform = physics::Simulator::renderPoseDelta(record.body) * record.transform;
		gathered.push_back(
		    {.id = id,
				 .volume = record.volume,
				 .palette = palette,
				 .model = drawn_transform,
				 .debug_name = "Voxel fragment ",
				 .fragment_slot = record.shape.slot}
		);
		key.push_back(
		    {.node_rid = id,
				 .revision = record.revision,
				 .content = record.volume->revision(),
				 .palette = palette,
				 .palette_revision = palette->revision}
		);
	}

	const bool keep_mirror = voxel_debug::isView(m_render_mode);

	// A pack still uploading is left alone rather than replaced every frame
	if (m_voxel_storage_pending == nullptr && (key != m_voxel_scene_key || keep_mirror != m_voxel_mirror_kept)) {
		std::vector<TrackedVolume> tracked;
		tracked.reserve(gathered.size());
		for (const Gathered& entry : gathered) {
			tracked.push_back(
			    {.node_rid = entry.id, .volume = entry.volume, .palette = entry.palette, .palette_revision = entry.palette->revision}
			);
		}
		std::vector<VolumeDelta> deltas;
		m_voxel_change_tracker.observe(
		    tracked,
		    [this](uint64_t node_rid) -> std::optional<glm::uvec3> {
			    const auto record = m_voxel_storage ? m_voxel_storage->recordIndexOf(node_rid) : std::nullopt;
			    return record.has_value() ? std::optional {m_voxel_storage->recordBrickDims(*record)} : std::nullopt;
		    },
		    &deltas
		);

		m_voxel_scene_key = key;
		m_voxel_mirror_kept = keep_mirror;
		m_voxel_upload_failed_warned = false;

		if (gathered.empty()) {
			m_voxel_storage.reset();
			m_voxel_storage_pending.reset();
			m_voxel_change_tracker.reset();
			m_voxel_patcher.reset();
		} else {
			std::vector<voxel::gpu::SceneVolume> scene_volumes;
			std::vector<uint64_t> node_rids;
			std::vector<glm::uvec3> brick_dims;
			VoxelStorageDebugInfo debug;
			scene_volumes.reserve(gathered.size());
			node_rids.reserve(gathered.size());
			brick_dims.reserve(gathered.size());
			debug.node_names.reserve(gathered.size());
			for (const Gathered& entry : gathered) {
				scene_volumes.push_back({.volume = entry.volume, .palette = entry.palette});
				node_rids.push_back(entry.id);
				brick_dims.push_back(entry.volume->brickDims());
				std::string name {entry.debug_name};
				name += entry.debug_suffix;
				if (entry.fragment_slot != ~0u) {
					name += std::to_string(entry.fragment_slot);
				}
				debug.node_names.push_back(std::move(name));
			}

			const auto pack_start = std::chrono::steady_clock::now();
			const auto elapsed_ms = [&pack_start] {
				return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - pack_start).count();
			};

			// Carves spawns and despawns patch the shared buffers in place and only running out of spare room uploads
			// everything again
			std::optional<VoxelScenePatcher::Patched> patched;
			if (!keep_mirror && m_voxel_storage != nullptr && m_voxel_storage->isReady()) {
				patched = m_voxel_patcher.tryPatch(voxel::runtimeBrickPool(), scene_volumes, node_rids, deltas);
			}
			std::optional<uint64_t> patch_sequence;
			if (patched.has_value()) {
				patch_sequence = m_voxel_storage->queuePatch(*m_core, patched->patch);
			}

			if (patch_sequence.has_value()) {
				debug.pack_ms = elapsed_ms();
				voxel_lock.unlock();

				const voxel::gpu::ScenePatch& patch = patched->patch;
				const VoxelStorageDebugInfo& previous = m_voxel_storage->debugInfo();
				debug.sequence = ++m_voxel_upload_sequence;
				debug.packed_slots = patch.pool_slots;
				debug.packed_palettes = patch.palette_words / voxel::gpu::k_palette_words;

				// Debug rows follow records and a dead record keeps an empty name
				std::unordered_map<uint64_t, std::string> names;
				for (size_t i = 0; i < node_rids.size(); ++i) {
					names.emplace(node_rids[i], std::move(debug.node_names[i]));
				}
				debug.node_names.assign(patched->record_rids.size(), std::string {});
				for (size_t i = 0; i < patched->record_rids.size(); ++i) {
					if (const auto name = names.find(patched->record_rids[i]); name != names.end()) {
						debug.node_names[i] = std::move(name->second);
					}
				}
				debug.bricks = previous.bricks;
				debug.bricks.resize(patched->record_rids.size());
				for (size_t i = 0; i < debug.bricks.size() && i < patch.tags.size(); ++i) {
					const auto apply = [](uint32_t count, int32_t delta) {
						return static_cast<uint32_t>(static_cast<int64_t>(count) + delta);
					};
					debug.bricks[i].uniform = apply(debug.bricks[i].uniform, patch.tags[i].uniform);
					debug.bricks[i].shared = apply(debug.bricks[i].shared, patch.tags[i].shared);
					debug.bricks[i].owned = apply(debug.bricks[i].owned, patch.tags[i].owned);
				}
				debug.patched = true;
				debug.patched_bricks = static_cast<uint32_t>(
				    std::accumulate(deltas.begin(), deltas.end(), size_t {0}, [](size_t sum, const VolumeDelta& delta) {
					    return sum + delta.bricks.size();
				    })
				);

				auto storage = std::make_shared<VoxelGpuStorage>(
				    std::move(patched->record_rids), std::move(patched->record_dims), *m_voxel_storage, *patch_sequence
				);
				auto glass = packedGlassBricks(m_voxel_patcher.scene());
				storage->setBrickLists(packedBrickLists(m_voxel_patcher.scene(), glass, *storage, m_voxel_storage.get(), deltas));
				storage->setGlassBricks(std::move(glass));
				storage->setDebugInfo(std::move(debug));
				storage->setChangeHistory(m_voxel_change_tracker.packed(storage->generation()));
				m_voxel_storage = std::move(storage);
				m_voxel_change_tracker.published();
			} else {
				if (m_voxel_storage != nullptr) {
					const char* reason;
					if (keep_mirror) {
						reason = "a voxel debug view keeps a CPU mirror";
					} else if (not m_voxel_storage->isReady()) {
						reason = "the previous upload is not ready";
					} else if (not patched.has_value()) {
						reason = "dead records or the edit outweigh half the scene";
					} else {
						reason = "the GPU buffers ran out of spare room";
					}

					TOAST_INFO("Render", "Voxel scene uploads every brick again: {}", reason);
				}
				auto packed = std::make_shared<VoxelPackedScene>(VoxelPackedScene {
				  .pool = voxel::gpu::packPool(voxel::runtimeBrickPool()), .scene = voxel::gpu::packScene(scene_volumes)
				});
				debug.pack_ms = elapsed_ms();
				voxel_lock.unlock();

				debug.sequence = ++m_voxel_upload_sequence;
				debug.packed_slots = static_cast<uint32_t>(packed->pool.materials.size() / voxel::gpu::k_material_words_per_brick);
				debug.packed_palettes = static_cast<uint32_t>(packed->scene.palettes.size() / voxel::gpu::k_palette_words);
				debug.bricks.reserve(packed->scene.records.size());
				for (const voxel::gpu::VolumeRecord& record : packed->scene.records) {
					VoxelStorageDebugInfo::BrickCensus census;
					const uint32_t count = record.brick_dims_x * record.brick_dims_y * record.brick_dims_z;
					for (uint32_t i = 0; i < count; ++i) {
						switch (voxel::BrickEntry {packed->scene.grids[record.grid_offset + i]}.tag()) {
							case voxel::BrickTag::uniform: ++census.uniform; break;
							case voxel::BrickTag::shared: ++census.shared; break;
							case voxel::BrickTag::owned: ++census.owned; break;
							case voxel::BrickTag::empty: break;
						}
					}
					debug.bricks.push_back(census);
				}
				if (keep_mirror) {
					debug.mirror = packed;
				}

				m_voxel_patcher.adopt(packed->scene, debug.packed_slots, node_rids);

				auto storage = std::make_shared<VoxelGpuStorage>(std::move(node_rids), std::move(brick_dims));
				auto glass = packedGlassBricks(packed->scene);
				storage->setBrickLists(packedBrickLists(packed->scene, glass, *storage, nullptr, {}));
				storage->setGlassBricks(std::move(glass));
				storage->setDebugInfo(std::move(debug));
				storage->setChangeHistory(m_voxel_change_tracker.packed(storage->generation()));
				queueResourceUpload(std::make_unique<VoxelSceneUpload>(storage, std::move(packed)));
				m_voxel_storage_pending = std::move(storage);
			}
		}
	}

	if (voxel_lock.owns_lock()) {
		voxel_lock.unlock();
	}

	frame.voxel_storage = m_voxel_storage;
	if (!m_voxel_storage) {
		m_voxel_previous_models.clear();
		m_voxel_drawn_models.clear();
		m_voxel_proxy_math.clear();
		m_voxel_collision_storage = nullptr;
		m_voxel_collision_count = 0;
		std::scoped_lock lock(m_voxel_collision_mutex);
		m_voxel_collision_world.reset();
		return;
	}

	// Existing keys are rewritten in place so a steady scene allocates nothing here
	const uint64_t proxy_frame = ++m_voxel_proxy_frame;
	frame.voxel_instances.reserve(gathered.size());
	bool collision_changed = m_voxel_collision_storage != m_voxel_storage.get();

	for (const Gathered& entry : gathered) {
		const uint64_t node_rid = entry.id;
		if (entry.outline_only) {
			if (const std::optional<uint32_t> record = m_voxel_storage->recordIndexOf(node_rid)) {
				const glm::uvec3 dims = m_voxel_storage->recordBrickDims(*record);
				const glm::vec4 sphere = voxel::worldBoundingSphere(entry.model, dims);
				frame.voxel_outline_only.push_back(
				    VoxelVolumeProxy {
				      .model = entry.model,
				      .inverse_model = glm::inverse(entry.model),
				      .previous_model = entry.model,
				      .node_rid = node_rid,
				      .brick_dims = dims,
				      .record_index = *record,
				      .bounds_center = glm::vec3(sphere),
				      .bounds_radius = sphere.w,
				      .mirrored = glm::determinant(entry.model) < 0.0f,
				      .outlined = true,
				    }
				);
			}
			continue;
		}
		if (entry.data_only) {
			continue;
		}
		const std::optional<uint32_t> record = m_voxel_storage->recordIndexOf(node_rid);
		if (!record.has_value()) {
			continue;
		}

		const glm::uvec3 dims = m_voxel_storage->recordBrickDims(*record);
		const glm::mat4& model = entry.model;
		auto [math_it, inserted] = m_voxel_proxy_math.try_emplace(node_rid);
		VoxelProxyMath& math = math_it->second;
		if (inserted || math.model != model || math.dims != dims || math.record != *record) {
			math.model = model;
			math.inverse = glm::inverse(model);
			math.sphere = voxel::worldBoundingSphere(model, dims);
			math.dims = dims;
			math.record = *record;
			math.mirrored = glm::determinant(model) < 0.0f;
			collision_changed = true;
		}
		math.frame = proxy_frame;

		const auto previous = m_voxel_previous_models.find(node_rid);
		const bool has_previous = previous != m_voxel_previous_models.end();

		frame.voxel_instances.push_back(
		    VoxelVolumeProxy {
		      .model = model,
		      .inverse_model = math.inverse,
		      .previous_model = has_previous ? previous->second.model : model,
		      .node_rid = node_rid,
		      .brick_dims = dims,
		      .record_index = *record,
		      .bounds_center = glm::vec3(math.sphere),
		      .bounds_radius = math.sphere.w,
		      .has_previous = has_previous,
		      .mirrored = math.mirrored,
		      .outlined = entry.piece_outlined || isOutlined(entry.outline_id != 0 ? entry.outline_id : node_rid),
		    }
		);
		m_voxel_drawn_models.insert_or_assign(node_rid, VoxelDrawnModel {.model = model, .frame = proxy_frame});
	}

	// Only sweeps when something left since a full walk costs as much as the frame saved
	const size_t drawn = frame.voxel_instances.size();
	if (m_voxel_drawn_models.size() > drawn) {
		std::erase_if(m_voxel_drawn_models, [&](const auto& item) { return item.second.frame != proxy_frame; });
	}
	if (m_voxel_proxy_math.size() > drawn) {
		std::erase_if(m_voxel_proxy_math, [&](const auto& item) { return item.second.frame != proxy_frame; });
	}

	collision_changed = collision_changed || m_voxel_collision_count != drawn;
	if (!collision_changed) {
		return;
	}
	m_voxel_collision_storage = m_voxel_storage.get();
	m_voxel_collision_count = drawn;

	auto collision = std::make_shared<VoxelCollisionWorld>();
	collision->storage = m_voxel_storage;
	collision->volumes.reserve(frame.voxel_instances.size());
	const glm::mat4 to_voxel_units = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / voxel::k_voxel_size));
	for (const VoxelVolumeProxy& proxy : frame.voxel_instances) {
		// the debris pass culls by the box of the volume
		const glm::vec3 extent = voxel::localExtent(proxy.brick_dims);
		glm::vec3 box_min {std::numeric_limits<float>::max()};
		glm::vec3 box_max {std::numeric_limits<float>::lowest()};
		for (int corner = 0; corner < 8; ++corner) {
			const glm::vec3 local(
			    (corner & 1) != 0 ? extent.x : 0.0f, (corner & 2) != 0 ? extent.y : 0.0f, (corner & 4) != 0 ? extent.z : 0.0f
			);
			const glm::vec3 world = glm::vec3(proxy.model * glm::vec4(local, 1.0f));
			box_min = glm::min(box_min, world);
			box_max = glm::max(box_max, world);
		}
		collision->volumes.push_back(
		    VoxelCollisionWorld::Volume {
		      .world_to_voxel = to_voxel_units * proxy.inverse_model,
		      .bounds_center = proxy.bounds_center,
		      .bounds_radius = proxy.bounds_radius,
		      .box_min = box_min,
		      .box_max = box_max,
		      .record_index = proxy.record_index,
		    }
		);
	}
	std::scoped_lock lock(m_voxel_collision_mutex);
	m_voxel_collision_world = std::move(collision);
}

}
