/// @file vulkan_renderer_record.cpp
/// @author dario
/// @date 16/05/2026

#include "toast/renderer/core/vulkan_debug.hpp"
#include "toast/renderer/debug/editor_overlays.hpp"
#include "toast/renderer/debug/voxel_debug.hpp"
#include "toast/renderer/lighting/clustered_lighting_constants.hpp"
#include "toast/renderer/lighting/cube_face_basis.hpp"
#include "toast/renderer/lighting/frustum.hpp"
#include "toast/renderer/lighting/light_culling.hpp"
#include "toast/renderer/lighting/shadow_slots.hpp"
#include "toast/renderer/passes/debug/selection_outline.hpp"
#include "toast/renderer/passes/geometry/debris_pass.hpp"
#include "toast/renderer/passes/geometry/depth_prepass.hpp"
#include "toast/renderer/passes/geometry/material_pass.hpp"
#include "toast/renderer/passes/geometry/skinning_pass.hpp"
#include "toast/renderer/passes/geometry/voxel_pass.hpp"
#include "toast/renderer/passes/lighting/environment_pass.hpp"
#include "toast/renderer/passes/lighting/gi_pass.hpp"
#include "toast/renderer/passes/lighting/reflection_probe_pass.hpp"
#include "toast/renderer/passes/lighting/shadow_pass.hpp"
#include "toast/renderer/passes/lighting/traced_specular_pass.hpp"
#include "toast/renderer/passes/post/taa_pass.hpp"
#include "toast/renderer/raytracing/ray_tracing_scene.hpp"
#include "toast/renderer/raytracing/skinned_blas_pool.hpp"
#include "toast/renderer/resources/blue_noise.hpp"
#include "toast/renderer/settings/renderer_settings.hpp"
#include "toast/renderer/shaders/shader_compiler.hpp"
#include "toast/renderer/vulkan_renderer.hpp"
#include "toast/renderer/vulkan_renderer_detail.hpp"

#include <SDL3/SDL_keycode.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <toast/assets/animation.hpp>
#include <toast/assets/asset_manager.hpp>
#include <toast/assets/assets.hpp>
#include <toast/assets/material.hpp>
#include <toast/log.hpp>
#include <toast/physics/simulator.hpp>
#include <toast/profiler_phases.hpp>
#include <toast/settings/settings.hpp>
#include <toast/thread_pool.hpp>
#include <toast/time.hpp>
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
#include <tracy/Tracy.hpp>
#include <tuple>
#include <unordered_set>

#if defined(_WIN32) && defined(TOAST_DEV_TOOLS)
#include <external/inc/nsight/NGFX_GPUTrace_Vulkan.h>
#include <external/inc/nsight/NGFX_GraphicsCapture_Vulkan.h>
#endif

#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace renderer {

#if defined(TOAST_DEV_TOOLS)
namespace {

/// Logs the scopes that grew when a frame runs far past the usual GPU time
void reportGpuSpike(const GpuTimer& timer) {
	using clock = std::chrono::steady_clock;
	constexpr size_t k_history = 120;
	constexpr double k_spike_ratio = 2.0;
	constexpr double k_min_spike_ms = 6.0;
	constexpr double k_average_blend = 0.05;
	constexpr auto k_report_every = std::chrono::seconds(1);

	// Render thread only
	static uint64_t last_serial = 0;
	static std::array<float, k_history> history {};
	static size_t filled = 0;
	static size_t cursor = 0;
	static std::unordered_map<std::string, double> averages;
	static clock::time_point last_report {};
	static uint32_t spikes_since_report = 0;

	const GpuTimer::Timings& timings = timer.last();
	if (!timings.valid || timer.frameSerial() == last_serial) {
		return;
	}
	last_serial = timer.frameSerial();

	std::array<float, k_history> sorted = history;
	const size_t count = filled;
	double median = 0.0;
	if (count >= 30) {
		std::nth_element(
		    sorted.begin(),
		    sorted.begin() + static_cast<std::ptrdiff_t>(count / 2),
		    sorted.begin() + static_cast<std::ptrdiff_t>(count)
		);
		median = sorted[count / 2];
	}
	const bool spike = median > 0.0 && timings.graphics_ms > median * k_spike_ratio && timings.graphics_ms > k_min_spike_ms;

	struct Growth {
		std::string name;
		double now = 0.0;
		double usual = 0.0;
	};

	std::vector<Growth> growth;
	for (const GpuTimer::ScopeTiming& scope : timings.scopes) {
		double& average = averages.try_emplace(std::string(scope.name.view()), scope.gpu_ms).first->second;
		if (spike) {
			growth.push_back({std::string(scope.name.view()), scope.gpu_ms, average});
		}
		// Spikes stay out of the average
		if (!spike) {
			average += (scope.gpu_ms - average) * k_average_blend;
		}
	}

	if (!spike) {
		history[cursor] = static_cast<float>(timings.graphics_ms);
		cursor = (cursor + 1) % k_history;
		filled = std::min(filled + 1, k_history);
		return;
	}

	++spikes_since_report;
	const auto now = clock::now();
	if (now - last_report < k_report_every) {
		return;
	}
	last_report = now;

	std::ranges::sort(growth, [](const Growth& a, const Growth& b) { return (a.now - a.usual) > (b.now - b.usual); });
	std::string worst;
	for (size_t i = 0; i < std::min<size_t>(growth.size(), 4); ++i) {
		worst += std::format("{}{} {:.1f} ms (usually {:.1f})", i == 0 ? "" : ", ", growth[i].name, growth[i].now, growth[i].usual);
	}
	TOAST_WARN(
	    "Render",
	    "GPU spike {:.1f} ms against a usual {:.1f}, {} spike(s) in the last second. Grew most: {}",
	    timings.graphics_ms,
	    median,
	    spikes_since_report,
	    worst
	);
	spikes_since_report = 0;
}

}
#endif

#ifdef TRACY_ENABLE
namespace {

/// One plot per timed scope, read a frame late behind the fence
void plotGpuTimings(const GpuTimer& timer) {
	static uint64_t last_serial = 0;
	const GpuTimer::Timings& timings = timer.last();
	if (!timings.valid || timer.frameSerial() == last_serial) {
		return;
	}
	last_serial = timer.frameSerial();

	// Node based so plot names keep their address
	static std::unordered_map<std::string, std::string> plot_names;
	TracyPlot("GPU: graphics ms", timings.graphics_ms);
	for (const GpuTimer::ScopeTiming& scope : timings.scopes) {
		std::string key(scope.name.view());
		auto it = plot_names.find(key);
		if (it == plot_names.end()) {
			it = plot_names.emplace(key, "GPU: " + key).first;
		}
		TracyPlot(it->second.c_str(), scope.gpu_ms);
	}
}

}
#endif

void VulkanRenderer::recordDepthUpscale(vk::CommandBuffer cmd, const vk::Rect2D& area) {
	ZoneScoped;
	const vk::ImageView scene_depth = **m_depth_resources.view;
	if (m_depth_upscale_bound[m_current_frame] != scene_depth) {
		const vk::DescriptorImageInfo image_info(*m_depth_upscale_sampler, scene_depth, vk::ImageLayout::eDepthReadOnlyOptimal);
		const vk::WriteDescriptorSet write(
		    *m_depth_upscale_sets[m_current_frame], 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &image_info
		);
		m_core->getDevice().updateDescriptorSets(write, {});
		m_depth_upscale_bound[m_current_frame] = scene_depth;
	}
	sync::transition(cmd, m_output_depth.state, sync::Usage::depth_attachment);

	vk::RenderingAttachmentInfo depth_info {};
	depth_info.imageView = **m_output_depth.view;
	depth_info.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
	depth_info.loadOp = vk::AttachmentLoadOp::eDontCare;
	depth_info.storeOp = vk::AttachmentStoreOp::eStore;

	vk::RenderingInfo info {};
	info.renderArea = area;
	info.layerCount = 1;
	info.pDepthAttachment = &depth_info;
	cmd.beginRendering(info);
	cmd.setViewport(
	    0,
	    std::array {
	      vk::Viewport(0.0f, 0.0f, static_cast<float>(area.extent.width), static_cast<float>(area.extent.height), 0.0f, 1.0f)
	    }
	);
	cmd.setScissor(0, std::array {area});
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_depth_upscale_pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    *m_present_layout.getPipelineLayout(),
	    0,
	    std::array<vk::DescriptorSet, 1> {*m_depth_upscale_sets[m_current_frame]},
	    {}
	);
	cmd.draw(3, 1, 0, 0);
	cmd.endRendering();
	sync::transition(cmd, m_output_depth.state, sync::Usage::depth_sampled);
}

void VulkanRenderer::publishCompletedFrames() {
	ZoneScoped;
	while (!m_pending_publish.empty()) {
		const uint32_t index = m_pending_publish.front();
		auto& pending = m_frames[index];

		if (!pending.has_submitted) {
			m_pending_publish.pop_front();
			continue;
		}

		if (pending.in_flight.getStatus() != vk::Result::eSuccess) {
			break;
		}

		m_output_target->onImageRenderComplete(pending.last_image_index, !pending.was_probe_capture);

		pending.has_submitted = false;
		m_pending_publish.pop_front();
	}
}

void VulkanRenderer::updateVoxelTraceScene() {
	ZoneScoped;
	const auto* render_frame = renderingFrame();
	if (m_voxel_trace_scene == nullptr || render_frame == nullptr) {
		return;
	}

	m_trace_volumes.clear();
	const VoxelGpuStorage* storage = render_frame->voxel_storage.get();
	if (storage != nullptr && storage->isReady()) {
		const auto cap = std::min<size_t>(voxelShadowInstances(), VoxelTraceScene::k_max_volumes);
		// Drawn volumes first so the cap drops off screen ones
		for (const bool visible : {true, false}) {
			for (const VoxelVolumeProxy& proxy : render_frame->voxel_instances) {
				if (proxy.visible != visible || m_trace_volumes.size() >= cap) {
					continue;
				}
				VoxelTraceScene::Volume volume {
				  .instance =
				      makeVoxelInstance(proxy.model, proxy.inverse_model, proxy.previous_model, proxy.has_previous, proxy.record_index),
				  .bounds = glm::vec4(proxy.bounds_center, proxy.bounds_radius),
				  .glass = !storage->glassBricks(proxy.record_index).empty(),
				  .voxel_dims = proxy.brick_dims * voxel::k_brick_dim,
				};
				if (proxy.mirrored) {
					volume.instance.flags |= k_voxel_instance_mirrored;
				}
				m_trace_volumes.push_back(volume);
			}
		}
	}
	m_voxel_trace_scene->update(m_current_frame, m_trace_volumes, render_frame->frame_data.camera_position, voxelTraceCellSize());
}

void VulkanRenderer::recordAccelerationStructureBuilds(FrameContext& frame) {
	ZoneScoped;
	if (!m_core->isRayTracingSupported()) {
		return;
	}

	const auto* render_frame = renderingFrame();
	if (render_frame == nullptr) {
		return;
	}

	constexpr uint32_t k_max_builds_per_frame = 8;
	uint32_t built = 0;

	std::unordered_set<VulkanMesh*> seen;
	for (const auto& proxy : render_frame->mesh_instances) {
		if (built >= k_max_builds_per_frame) {
			break;
		}
		if (proxy.mesh == nullptr || !proxy.mesh->isReady() || proxy.mesh->hasAccelerationStructure()) {
			continue;
		}
		if (!seen.insert(proxy.mesh).second) {
			continue;
		}

		auto scratch = proxy.mesh->createAccelerationStructure(*m_core);
		if (!scratch.has_value()) {
			continue;
		}

		proxy.mesh->recordBuildAccelerationStructure(*frame.command_buffer);
		frame.blas_scratch.push_back(std::move(*scratch));
		++built;
	}

	const bool tracing = render_frame->frame_data.traced_shadow_params.x >= 0.5f;

	uint32_t skinned_recorded = 0;
	if (m_skinned_blas_pool != nullptr && tracing) {
		m_skinned_blas_pool->beginFrame();

		const vk::Buffer posed = getPosedVertexBuffer(m_current_frame);
		for (const auto& proxy : render_frame->mesh_instances) {
			if (proxy.mesh == nullptr || !proxy.mesh->isReady() ||
			    proxy.posed_vertex_offset == MeshInstanceProxy::k_no_posed_vertices) {
				continue;
			}
			if (m_skinned_blas_pool->recordFor(*frame.command_buffer, proxy.node_rid, *proxy.mesh, posed, proxy.posed_vertex_offset) !=
			    0) {
				++skinned_recorded;
			}
		}

		m_skinned_blas_pool->endFrame();
	}

	if (built > 0 || skinned_recorded > 0) {
		// BLAS builds before the TLAS build that references them and before any fragment ray query
		sync::BarrierBatch barriers;
		barriers.memory(sync::Usage::acceleration_build, sync::Usage::acceleration_build);
		barriers.memory(sync::Usage::acceleration_build, sync::Usage::acceleration_trace_fragment);
		barriers.flush(*frame.command_buffer);
	}

	if (m_ray_tracing_scene != nullptr) {
		m_ray_tracing_scene->beginFrame();
		for (const auto& proxy : render_frame->mesh_instances) {
			if (!detail::isTraceable(proxy)) {
				continue;
			}

			const bool posed = proxy.posed_vertex_offset != MeshInstanceProxy::k_no_posed_vertices;
			vk::DeviceAddress blas_address = 0;
			if (!posed) {
				blas_address = proxy.mesh->getAccelerationStructureAddress();
			} else if (m_skinned_blas_pool != nullptr) {
				blas_address = m_skinned_blas_pool->addressFor(proxy.node_rid);
			}
			if (blas_address == 0) {
				continue;
			}

			m_ray_tracing_scene->addInstance(
			    {.transform = posed ? glm::mat4(1.0f) : proxy.model, .blas_address = blas_address, .mask = 0x01, .deforming = posed}
			);
		}
		const bool queries = render_frame->frame_data.traced_shadow_params.y >= 0.5f && m_voxel_trace_scene != nullptr;
		const VoxelGpuStorage* storage = render_frame->voxel_storage.get();
		const bool bricks =
		    queries && m_voxel_blas_cache != nullptr && voxelTraceBricks() && storage != nullptr && storage->isReady();
		if (bricks) {
			const GpuScope scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, "Voxel BLAS");
			m_trace_records.clear();
			for (const VoxelTraceScene::Volume& volume : m_trace_volumes) {
				m_trace_records.push_back(volume.instance.record_index);
			}
			m_voxel_blas_cache->update(*frame.command_buffer, m_current_frame, *storage, m_trace_records, voxelBlasBudget());
		}
		const bool voxel_queries =
		    queries &&
		    m_voxel_trace_scene->addRayQueryInstances(
		        *frame.command_buffer, m_current_frame, *m_ray_tracing_scene, bricks ? m_voxel_blas_cache.get() : nullptr, storage
		    );
		m_ray_tracing_scene->build(*frame.command_buffer, m_current_frame, tracing || voxel_queries);
	}
}

void VulkanRenderer::recordMeshScene(
    vk::CommandBuffer cmd, uint32_t image_index, bool voxel_glass, const std::function<void()>& capture_backdrop
) {
	// Opaque first since m_material_passes is unordered
	{
		const GpuScope scope(m_gpu_timer.get(), m_current_frame, cmd, "Opaque materials");
		for (auto& [material, pass] : m_material_passes) {
			if (!pass->isEnabled() || pass->isBlended()) {
				continue;
			}
			TracyVkZone(m_tracy_vk_ctx, cmd, "MaterialPass");
			pass->record(cmd, m_current_frame, image_index);
		}
	}

	// Before blended draws since the sky overwrites far plane pixels
	if (m_environment_pass != nullptr && m_environment_pass->isEnabled()) {
		TracyVkZone(m_tracy_vk_ctx, cmd, "WorldPass");
		const GpuScope scope(m_gpu_timer.get(), m_current_frame, cmd, m_environment_pass->name());
		m_environment_pass->record(cmd, m_current_frame, image_index);
	}

	// Before anything blended so the backdrop holds only opaque and sky
	const bool has_glass = voxel_glass && m_voxel_pass != nullptr && !m_voxel_pass->glassDraws().empty() && capture_backdrop;
	const bool gi_active = m_gi_pass != nullptr && m_gi_pass->isEnabled();
	const bool refract = has_glass && m_voxel_pass->refractsGlass();
	m_glass_backdrop_captured = has_glass && gi_active;
	if (refract || m_glass_backdrop_captured) {
		const GpuScope scope(m_gpu_timer.get(), m_current_frame, cmd, "Glass backdrop");
		capture_backdrop();
	}
	if (m_voxel_pass != nullptr) {
		m_voxel_pass->setBackdropCaptured(refract);
	}

	{
		const auto* render_frame = renderingFrame();
		if (render_frame != nullptr) {
			struct BlendedDraw {
				float distance_squared = 0.0f;
				MaterialPass* pass = nullptr;
				const MeshInstanceProxy* proxy = nullptr;
			};

			std::vector<BlendedDraw> draws;
			const glm::vec3 camera_position = render_frame->frame_data.camera_position;

			for (auto& [material, pass] : m_material_passes) {
				if (!pass->isEnabled() || !pass->isBlended()) {
					continue;
				}
				for (const auto& proxy : render_frame->mesh_instances) {
					if (proxy.root_material != pass->rootMaterial() || !proxy.visible) {
						continue;
					}
					const glm::vec3 delta = glm::vec3(proxy.model[3]) - camera_position;
					draws.push_back({glm::dot(delta, delta), pass.get(), &proxy});
				}
			}

			std::ranges::sort(draws, [](const BlendedDraw& a, const BlendedDraw& b) {
				if (a.distance_squared != b.distance_squared) {
					return a.distance_squared > b.distance_squared;
				}
				return a.pass < b.pass;
			});

			const std::span<const VoxelPass::GlassDraw> glass =
			    voxel_glass && m_voxel_pass != nullptr ? m_voxel_pass->glassDraws() : std::span<const VoxelPass::GlassDraw> {};
			size_t next_glass = 0;
			// Glass bricks at least as far as the next mesh go first as one run
			const auto record_glass_until = [&](float distance_squared) {
				size_t end = next_glass;
				while (end < glass.size() && glass[end].distance_squared >= distance_squared) {
					++end;
				}
				if (end > next_glass) {
					m_voxel_pass->recordGlass(
					    cmd, m_current_frame, static_cast<uint32_t>(next_glass), static_cast<uint32_t>(end - next_glass)
					);
					next_glass = end;
				}
			};

			TracyVkZone(m_tracy_vk_ctx, cmd, "BlendedDraws");
			const GpuScope scope(m_gpu_timer.get(), m_current_frame, cmd, "Blended materials and glass");
			for (const auto& draw : draws) {
				record_glass_until(draw.distance_squared);
				draw.pass->recordInstance(cmd, m_current_frame, *draw.proxy);
			}
			record_glass_until(-1.0f);

			// One run after voxel glass since the pieces are small and mostly in front of it
			if (m_debris_pass != nullptr && m_debris_pass->isEnabled()) {
				m_debris_pass->recordGlass(cmd, m_current_frame);
			}

			// Last so the glass depth it writes never rejects a blended draw behind the glass
			if (!glass.empty()) {
				m_voxel_pass->recordGlassSurfaces(cmd, m_current_frame);
			}
		}
	}
}

auto VulkanRenderer::recordVoxelScopes(
    FrameContext& frame, const vk::Viewport& viewport, const vk::Rect2D& scissor, bool depth_loaded
) -> bool {
	ZoneScoped;
	m_traced_specular_frame = false;
	const bool targets_ready =
	    m_depth_resources.view.has_value() && m_voxel_albedo.view.has_value() && m_voxel_material.view.has_value() &&
	    m_voxel_face_normal.view.has_value() && m_scene_color.view.has_value() && m_scene_normal.view.has_value() &&
	    m_scene_indirect.view.has_value() && m_scene_motion.view.has_value() && m_voxel_noisy_diffuse.view.has_value() &&
	    m_voxel_noisy_specular.view.has_value() && m_voxel_reservoirs[0].view.has_value() &&
	    m_voxel_reservoirs[1].view.has_value() && m_voxel_candidate_reservoirs.view.has_value();
	if (m_voxel_pass == nullptr || !m_voxel_pass->isEnabled() || !targets_ready ||
	    m_depth_resources.state.usage != sync::Usage::depth_attachment || !m_voxel_pass->prepare(m_current_frame)) {
		return false;
	}
	// Decided once since voxel lighting and the post pass must agree on which pixels lost their environment specular
	const auto* specular_frame = renderingFrame();
	m_traced_specular_frame = m_traced_specular_pass != nullptr && m_traced_specular_pass->isEnabled() &&
	                          m_traced_specular_pass->isReady() && voxelTracedSpecular() && specular_frame != nullptr &&
	                          specular_frame->render_mode == 0 && specular_frame->probe_capture_index < 0 &&
	                          specular_frame->irradiance_capture_index < 0;

	const vk::CommandBuffer cmd = *frame.command_buffer;
	{
		TracyVkZone(m_tracy_vk_ctx, cmd, "VoxelCells");
		const GpuScope scope(m_gpu_timer.get(), m_current_frame, cmd, "Voxel cells");
		m_voxel_pass->recordCellCull(cmd, m_current_frame, scissor.extent);
	}

	const bool debris_drawn =
	    m_debris_pass != nullptr && m_debris_pass->isEnabled() && m_debris_pass->prepare(cmd, m_current_frame);
	sync::BarrierBatch barriers;

	const auto attachment = [](const SceneColorResources& target, vk::AttachmentLoadOp load, const vk::ClearValue& clear) {
		vk::RenderingAttachmentInfo info {};
		info.imageView = **target.view;
		info.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
		info.resolveMode = vk::ResolveModeFlagBits::eNone;
		info.loadOp = load;
		info.storeOp = vk::AttachmentStoreOp::eStore;
		info.clearValue = clear;
		return info;
	};

	const vk::ClearValue clear_zero(vk::ClearColorValue(std::array {0.0f, 0.0f, 0.0f, 0.0f}));
	const vk::ClearValue clear_scene(vk::ClearColorValue(std::array {0.0f, 0.0f, 0.0f, 1.0f}));

	// Depth stays an attachment but the prepass writes must land before this scope loads them
	barriers.image(m_depth_resources.state, sync::Usage::depth_attachment);
	barriers.image(m_voxel_albedo.state, sync::Usage::color_attachment);
	barriers.image(m_voxel_face_normal.state, sync::Usage::color_attachment);
	barriers.image(m_voxel_material.state, sync::Usage::color_attachment);
	barriers.image(m_scene_motion.state, sync::Usage::color_attachment);
	barriers.flush(cmd);

	// Order matches voxel.slang FSOutput
	std::array gbuffer_attachments {
	  attachment(m_voxel_albedo, vk::AttachmentLoadOp::eClear, clear_zero),
	  attachment(m_voxel_face_normal, vk::AttachmentLoadOp::eClear, clear_zero),
	  attachment(m_voxel_material, vk::AttachmentLoadOp::eClear, clear_zero),
	  attachment(m_scene_motion, vk::AttachmentLoadOp::eClear, clear_zero),
	};

	vk::RenderingAttachmentInfo depth_attachment {};
	depth_attachment.imageView = **m_depth_resources.view;
	depth_attachment.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
	depth_attachment.loadOp = depth_loaded ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear;
	depth_attachment.storeOp = vk::AttachmentStoreOp::eStore;
	depth_attachment.clearValue = vk::ClearDepthStencilValue {depth::k_clear, 0};

	// Near quads lay depth first so the G-buffer scope shades one quad fragment per pixel
	const auto record_quad_depth = [&](bool late) {
		if (!m_voxel_pass->nearQuadDepthPass()) {
			return;
		}
		vk::RenderingInfo depth_info {};
		depth_info.renderArea = scissor;
		depth_info.layerCount = 1;
		depth_info.pDepthAttachment = &depth_attachment;

		cmd.beginRendering(depth_info);
		cmd.setViewport(0, std::array {viewport});
		cmd.setScissor(0, std::array {scissor});
		{
			TracyVkZone(m_tracy_vk_ctx, cmd, "VoxelQuadDepth");
			const GpuScope scope(m_gpu_timer.get(), m_current_frame, cmd, late ? "Voxel late quad depth" : "Voxel quad depth");
			m_voxel_pass->recordQuadDepth(cmd, m_current_frame, late);
		}
		cmd.endRendering();

		depth_attachment.loadOp = vk::AttachmentLoadOp::eLoad;
		barriers.image(m_depth_resources.state, sync::Usage::depth_attachment);
		barriers.flush(cmd);
	};
	record_quad_depth(false);

	vk::RenderingInfo gbuffer_info {};
	gbuffer_info.renderArea = scissor;
	gbuffer_info.layerCount = 1;
	gbuffer_info.colorAttachmentCount = static_cast<uint32_t>(gbuffer_attachments.size());
	gbuffer_info.pColorAttachments = gbuffer_attachments.data();
	gbuffer_info.pDepthAttachment = &depth_attachment;

	cmd.beginRendering(gbuffer_info);
	cmd.setViewport(0, std::array {viewport});
	cmd.setScissor(0, std::array {scissor});
	{
		TracyVkZone(m_tracy_vk_ctx, cmd, "VoxelGbuffer");
		const GpuScope scope(m_gpu_timer.get(), m_current_frame, cmd, "Voxel G-buffer");
		m_voxel_pass->record(cmd, m_current_frame, 0);
	}
	if (debris_drawn) {
		TracyVkZone(m_tracy_vk_ctx, cmd, "Debris");
		const GpuScope scope(m_gpu_timer.get(), m_current_frame, cmd, "Debris");
		m_debris_pass->record(cmd, m_current_frame, 0);
	}
	cmd.endRendering();

	if (m_voxel_pass->occlusionActive()) {
		barriers.image(m_depth_resources.state, sync::Usage::depth_sampled);
		barriers.flush(cmd);
		{
			TracyVkZone(m_tracy_vk_ctx, cmd, "VoxelOcclusion");
			const GpuScope scope(m_gpu_timer.get(), m_current_frame, cmd, "Voxel occlusion");
			m_voxel_pass->recordOcclusion(cmd, m_current_frame, scissor.extent);
		}

		// The late scope loads what the early one stored
		barriers.image(m_depth_resources.state, sync::Usage::depth_attachment);
		barriers.image(m_voxel_albedo.state, sync::Usage::color_attachment);
		barriers.image(m_voxel_face_normal.state, sync::Usage::color_attachment);
		barriers.image(m_voxel_material.state, sync::Usage::color_attachment);
		barriers.image(m_scene_motion.state, sync::Usage::color_attachment);
		barriers.flush(cmd);

		for (auto& gbuffer_attachment : gbuffer_attachments) {
			gbuffer_attachment.loadOp = vk::AttachmentLoadOp::eLoad;
		}
		depth_attachment.loadOp = vk::AttachmentLoadOp::eLoad;
		record_quad_depth(true);

		cmd.beginRendering(gbuffer_info);
		cmd.setViewport(0, std::array {viewport});
		cmd.setScissor(0, std::array {scissor});
		{
			TracyVkZone(m_tracy_vk_ctx, cmd, "VoxelLateGbuffer");
			const GpuScope scope(m_gpu_timer.get(), m_current_frame, cmd, "Voxel late G-buffer");
			m_voxel_pass->recordLate(cmd, m_current_frame);
		}
		cmd.endRendering();
	}

	barriers.image(m_voxel_albedo.state, sync::Usage::fragment_sampled);
	barriers.image(m_voxel_face_normal.state, sync::Usage::fragment_sampled);
	barriers.image(m_voxel_material.state, sync::Usage::fragment_sampled);
	barriers.image(m_depth_resources.state, sync::Usage::depth_sampled);
	barriers.image(m_scene_color.state, sync::Usage::color_attachment);
	barriers.image(m_scene_indirect.state, sync::Usage::color_attachment);
	barriers.image(m_scene_normal.state, sync::Usage::color_attachment);
	barriers.image(m_voxel_noisy_diffuse.state, sync::Usage::color_attachment);
	barriers.image(m_voxel_noisy_specular.state, sync::Usage::color_attachment);
	const bool spatial = m_voxel_pass->spatialActive();
	SceneColorResources& lighting_reservoirs =
	    spatial ? m_voxel_candidate_reservoirs : m_voxel_reservoirs[m_voxel_reservoir_parity];
	barriers.image(lighting_reservoirs.state, sync::Usage::color_attachment);
	barriers.image(m_voxel_reservoirs[1 - m_voxel_reservoir_parity].state, sync::Usage::fragment_sampled);
	barriers.image(m_scene_motion.state, sync::Usage::fragment_sampled);
	barriers.flush(cmd);

	// A frame without voxel lighting in between or a probe capture leaves reservoirs another view wrote
	const auto* lit_frame = renderingFrame();
	const uint64_t sequence = lit_frame != nullptr ? lit_frame->sequence : 0;
	const bool capture = lit_frame != nullptr && (lit_frame->probe_capture_index >= 0 || lit_frame->irradiance_capture_index >= 0);
	m_voxel_reservoir_history = !capture && m_voxel_reservoir_written && sequence - m_voxel_reservoir_sequence <= 1;

	// Clears what no voxel covers since the world scope now loads instead
	const std::array lighting_attachments {
	  attachment(m_scene_color, vk::AttachmentLoadOp::eClear, clear_scene),
	  attachment(m_scene_indirect, vk::AttachmentLoadOp::eClear, clear_zero),
	  attachment(m_scene_normal, vk::AttachmentLoadOp::eClear, clear_zero),
	  attachment(m_voxel_noisy_diffuse, vk::AttachmentLoadOp::eClear, clear_zero),
	};

	vk::RenderingInfo lighting_info {};
	lighting_info.renderArea = scissor;
	lighting_info.layerCount = 1;
	lighting_info.colorAttachmentCount = static_cast<uint32_t>(lighting_attachments.size());
	lighting_info.pColorAttachments = lighting_attachments.data();

	cmd.beginRendering(lighting_info);
	cmd.setViewport(0, std::array {viewport});
	cmd.setScissor(0, std::array {scissor});
	{
		TracyVkZone(m_tracy_vk_ctx, cmd, "VoxelLighting");
		const GpuScope scope(m_gpu_timer.get(), m_current_frame, cmd, "Voxel lighting");
		m_voxel_pass->recordLighting(cmd, m_current_frame, scissor.extent);
	}
	cmd.endRendering();

	barriers.image(m_scene_normal.state, sync::Usage::fragment_sampled);
	barriers.image(m_scene_color.state, sync::Usage::color_attachment);
	barriers.image(m_voxel_noisy_diffuse.state, sync::Usage::color_attachment);
	barriers.flush(cmd);

	// Before the marched scope so occlusion scales only the indirect
	if (m_voxel_pass->ambientRaysActive()) {
		const std::array ambient_attachments {attachment(m_voxel_noisy_diffuse, vk::AttachmentLoadOp::eLoad, clear_zero)};
		vk::RenderingInfo ambient_info {};
		ambient_info.renderArea = scissor;
		ambient_info.layerCount = 1;
		ambient_info.colorAttachmentCount = static_cast<uint32_t>(ambient_attachments.size());
		ambient_info.pColorAttachments = ambient_attachments.data();

		cmd.beginRendering(ambient_info);
		cmd.setViewport(0, std::array {viewport});
		cmd.setScissor(0, std::array {scissor});
		{
			TracyVkZone(m_tracy_vk_ctx, cmd, "VoxelAmbientRays");
			const GpuScope scope(m_gpu_timer.get(), m_current_frame, cmd, "Voxel ambient rays");
			m_voxel_pass->recordAmbient(cmd, m_current_frame);
		}
		cmd.endRendering();
		barriers.image(m_voxel_noisy_diffuse.state, sync::Usage::color_attachment);
		barriers.flush(cmd);
	}

	// Always recorded so the reservoirs and noisy specular clear even when nothing marches
	const std::array marched_attachments {
	  attachment(m_scene_color, vk::AttachmentLoadOp::eLoad, clear_scene),
	  attachment(m_voxel_noisy_diffuse, vk::AttachmentLoadOp::eLoad, clear_zero),
	  attachment(m_voxel_noisy_specular, vk::AttachmentLoadOp::eClear, clear_zero),
	  attachment(lighting_reservoirs, vk::AttachmentLoadOp::eClear, clear_zero),
	};
	vk::RenderingInfo marched_info {};
	marched_info.renderArea = scissor;
	marched_info.layerCount = 1;
	marched_info.colorAttachmentCount = static_cast<uint32_t>(marched_attachments.size());
	marched_info.pColorAttachments = marched_attachments.data();

	cmd.beginRendering(marched_info);
	cmd.setViewport(0, std::array {viewport});
	cmd.setScissor(0, std::array {scissor});
	{
		TracyVkZone(m_tracy_vk_ctx, cmd, "VoxelMarchedLighting");
		const GpuScope scope(m_gpu_timer.get(), m_current_frame, cmd, "Voxel marched lighting");
		m_voxel_pass->recordMarched(cmd, m_current_frame);
	}
	cmd.endRendering();

	if (spatial) {
		barriers.image(m_voxel_candidate_reservoirs.state, sync::Usage::fragment_sampled);
		barriers.image(m_voxel_reservoirs[m_voxel_reservoir_parity].state, sync::Usage::color_attachment);
		barriers.image(m_voxel_noisy_diffuse.state, sync::Usage::color_attachment);
		barriers.image(m_voxel_noisy_specular.state, sync::Usage::color_attachment);
		barriers.flush(cmd);

		const std::array spatial_attachments {
		  attachment(m_voxel_noisy_diffuse, vk::AttachmentLoadOp::eLoad, clear_zero),
		  attachment(m_voxel_noisy_specular, vk::AttachmentLoadOp::eLoad, clear_zero),
		  attachment(m_voxel_reservoirs[m_voxel_reservoir_parity], vk::AttachmentLoadOp::eClear, clear_zero),
		};
		vk::RenderingInfo spatial_info {};
		spatial_info.renderArea = scissor;
		spatial_info.layerCount = 1;
		spatial_info.colorAttachmentCount = static_cast<uint32_t>(spatial_attachments.size());
		spatial_info.pColorAttachments = spatial_attachments.data();

		cmd.beginRendering(spatial_info);
		cmd.setViewport(0, std::array {viewport});
		cmd.setScissor(0, std::array {scissor});
		{
			TracyVkZone(m_tracy_vk_ctx, cmd, "VoxelSpatialReuse");
			const GpuScope scope(m_gpu_timer.get(), m_current_frame, cmd, "Voxel spatial reuse");
			m_voxel_pass->recordSpatial(cmd, m_current_frame, scissor.extent);
		}
		cmd.endRendering();
	}
	m_voxel_reservoir_parity = 1 - m_voxel_reservoir_parity;
	m_voxel_reservoir_written = !capture;
	m_voxel_reservoir_sequence = sequence;

	if (m_voxel_pass->denoiseActive()) {
		barriers.image(m_voxel_noisy_diffuse.state, sync::Usage::fragment_sampled);
		barriers.image(m_voxel_noisy_specular.state, sync::Usage::fragment_sampled);
		barriers.image(m_scene_motion.state, sync::Usage::fragment_sampled);
		barriers.flush(cmd);
		{
			TracyVkZone(m_tracy_vk_ctx, cmd, "VoxelDenoiseTemporal");
			const GpuScope scope(m_gpu_timer.get(), m_current_frame, cmd, "Voxel denoise temporal");
			m_voxel_pass->recordDenoiseTemporal(cmd, m_current_frame);
		}

		barriers.image(m_scene_color.state, sync::Usage::color_attachment);
		barriers.flush(cmd);
		const std::array composite_attachments {attachment(m_scene_color, vk::AttachmentLoadOp::eLoad, clear_scene)};
		vk::RenderingInfo composite_info {};
		composite_info.renderArea = scissor;
		composite_info.layerCount = 1;
		composite_info.colorAttachmentCount = static_cast<uint32_t>(composite_attachments.size());
		composite_info.pColorAttachments = composite_attachments.data();

		cmd.beginRendering(composite_info);
		cmd.setViewport(0, std::array {viewport});
		cmd.setScissor(0, std::array {scissor});
		{
			TracyVkZone(m_tracy_vk_ctx, cmd, "VoxelDenoiseComposite");
			const GpuScope scope(m_gpu_timer.get(), m_current_frame, cmd, "Voxel denoise composite");
			m_voxel_pass->recordDenoiseComposite(cmd, m_current_frame);
		}
		cmd.endRendering();
	} else if (m_voxel_pass->marchedCompositeActive()) {
		barriers.image(m_voxel_noisy_diffuse.state, sync::Usage::fragment_sampled);
		barriers.image(m_voxel_noisy_specular.state, sync::Usage::fragment_sampled);
		barriers.image(m_scene_color.state, sync::Usage::color_attachment);
		barriers.flush(cmd);
		const std::array composite_attachments {attachment(m_scene_color, vk::AttachmentLoadOp::eLoad, clear_scene)};
		vk::RenderingInfo composite_info {};
		composite_info.renderArea = scissor;
		composite_info.layerCount = 1;
		composite_info.colorAttachmentCount = static_cast<uint32_t>(composite_attachments.size());
		composite_info.pColorAttachments = composite_attachments.data();

		cmd.beginRendering(composite_info);
		cmd.setViewport(0, std::array {viewport});
		cmd.setScissor(0, std::array {scissor});
		{
			TracyVkZone(m_tracy_vk_ctx, cmd, "VoxelMarchedComposite");
			const GpuScope scope(m_gpu_timer.get(), m_current_frame, cmd, "Voxel marched composite");
			m_voxel_pass->recordMarchedComposite(cmd, m_current_frame);
		}
		cmd.endRendering();
	}

	// The world scope loads all of these so writes above must land first
	barriers.image(m_scene_normal.state, sync::Usage::color_attachment);
	barriers.image(m_scene_color.state, sync::Usage::color_attachment);
	barriers.image(m_scene_indirect.state, sync::Usage::color_attachment);
	barriers.image(m_scene_motion.state, sync::Usage::color_attachment);
	barriers.image(m_depth_resources.state, sync::Usage::depth_attachment);
	barriers.flush(cmd);
	return true;
}

auto VulkanRenderer::recordFrame(FrameContext& frame, uint32_t image_index) noexcept -> void {
	ZoneScoped;
	frame.command_buffer.reset();
	const vk::CommandBufferBeginInfo begin_info(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	frame.command_buffer.begin(begin_info);
	if (m_gpu_timer != nullptr) {
		m_gpu_timer->beginGraphics(m_current_frame, *frame.command_buffer);
	}

	m_pending_acquires.flush(*frame.command_buffer);

	// Small carves patch the voxel sections in place before anything this frame reads them
	if (const auto* rendering = renderingFrame(); rendering != nullptr && rendering->voxel_storage != nullptr) {
		std::vector<std::shared_ptr<void>> staging;
		rendering->voxel_storage->recordPendingPatches(*frame.command_buffer, staging);
		for (auto& buffer : staging) {
			retire(std::move(buffer));
		}
	}

	// Graphics queue since this frame reads it and the compute queue belongs to AsyncCompute
	if (!m_compute_passes.empty()) {
		const GpuScope scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, "Light culling");
		for (auto& pass : m_compute_passes) {
			pass->dispatch(*frame.command_buffer, m_current_frame);
		}
		sync::BarrierBatch compute_done;
		compute_done.memory(sync::Usage::compute_storage_write, sync::Usage::fragment_storage_read);
		compute_done.flush(*frame.command_buffer);
	}

	// Before the AS builds which read its output
	if (m_skinning_pass != nullptr) {
		const GpuScope scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, "Skinning");
		m_skinning_pass->record(*frame.command_buffer, m_current_frame);
	}

	frame.blas_scratch.clear();
	updateVoxelTraceScene();
	{
		const GpuScope scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, "Acceleration structures");
		recordAccelerationStructureBuilds(frame);
	}

	{
		const GpuScope offscreen_scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, "Off-screen");
		for (auto& pass : m_render_passes) {
			const GpuScope scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, pass->name());
			pass->recordPre(*frame.command_buffer, m_current_frame, image_index);
		}
	}

	const auto extent = m_output_target->getExtent();
	const vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f);
	const vk::Rect2D scissor({0, 0}, extent);

	const auto* capture_source = renderingFrame();
	const uint32_t capture_size = capture_source != nullptr ? capture_source->capture_extent : 0u;
	const vk::Extent2D render_extent = renderExtent();
	const vk::Extent2D scene_extent = capture_size > 0 ? vk::Extent2D {capture_size, capture_size} : render_extent;
	const vk::Viewport scene_viewport(
	    0.0f, 0.0f, static_cast<float>(scene_extent.width), static_cast<float>(scene_extent.height), 0.0f, 1.0f
	);
	const vk::Rect2D scene_scissor({0, 0}, scene_extent);

	const vk::Image scene_image = m_scene_color.image ? **m_scene_color.image : VK_NULL_HANDLE;
	const vk::Image normal_image = m_scene_normal.image ? **m_scene_normal.image : VK_NULL_HANDLE;
	const vk::Image indirect_image = m_scene_indirect.image ? **m_scene_indirect.image : VK_NULL_HANDLE;
	const vk::Image motion_image = m_scene_motion.image ? **m_scene_motion.image : VK_NULL_HANDLE;

	// Last frame sampled these and its overlay scope stored depth
	sync::BarrierBatch barriers;
	barriers.image(m_scene_color.state, sync::Usage::color_attachment);
	barriers.image(m_scene_normal.state, sync::Usage::color_attachment);
	barriers.image(m_scene_indirect.state, sync::Usage::color_attachment);
	barriers.image(m_scene_motion.state, sync::Usage::color_attachment);
	barriers.image(m_depth_resources.state, sync::Usage::depth_attachment);
	// Glass binds the backdrop every frame whether or not it was captured
	if (m_glass_backdrop.image && m_glass_backdrop_depth.image) {
		barriers.image(m_glass_backdrop.state, sync::Usage::fragment_sampled);
		barriers.image(m_glass_backdrop_depth.state, sync::Usage::depth_sampled);
	}
	barriers.flush(*frame.command_buffer);

	const vk::ClearValue clear_color(vk::ClearColorValue(std::array {0.0f, 0.0f, 0.0f, 1.0f}));
	const vk::ClearValue clear_depth(vk::ClearDepthStencilValue {depth::k_clear, 0});

	vk::RenderingAttachmentInfo scene_attachment_info {};
	scene_attachment_info.imageView = scene_image != VK_NULL_HANDLE ? **m_scene_color.view : nullptr;
	scene_attachment_info.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
	scene_attachment_info.resolveMode = vk::ResolveModeFlagBits::eNone;
	scene_attachment_info.loadOp = vk::AttachmentLoadOp::eClear;
	scene_attachment_info.storeOp = vk::AttachmentStoreOp::eStore;
	scene_attachment_info.clearValue = clear_color;

	const vk::ClearValue clear_normal(vk::ClearColorValue(std::array {0.0f, 0.0f, 0.0f, 0.0f}));

	vk::RenderingAttachmentInfo normal_attachment_info {};
	normal_attachment_info.imageView = normal_image != VK_NULL_HANDLE ? **m_scene_normal.view : nullptr;
	normal_attachment_info.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
	normal_attachment_info.resolveMode = vk::ResolveModeFlagBits::eNone;
	normal_attachment_info.loadOp = vk::AttachmentLoadOp::eClear;
	normal_attachment_info.storeOp = vk::AttachmentStoreOp::eStore;
	normal_attachment_info.clearValue = clear_normal;

	const bool prepass_active = m_depth_prepass != nullptr && m_depth_prepass->isReady() && m_depth_resources.view.has_value();

	if (prepass_active) {
		vk::RenderingAttachmentInfo prepass_depth {};
		prepass_depth.imageView = **m_depth_resources.view;
		prepass_depth.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
		prepass_depth.loadOp = vk::AttachmentLoadOp::eClear;
		prepass_depth.storeOp = vk::AttachmentStoreOp::eStore;
		prepass_depth.clearValue = clear_depth;

		vk::RenderingInfo prepass_info {};
		prepass_info.renderArea = scene_scissor;
		prepass_info.layerCount = 1;
		prepass_info.colorAttachmentCount = 0;
		prepass_info.pDepthAttachment = &prepass_depth;

		frame.command_buffer.beginRendering(prepass_info);
		frame.command_buffer.setViewport(0, std::array {scene_viewport});
		frame.command_buffer.setScissor(0, std::array {scene_scissor});
		{
			TracyVkZone(m_tracy_vk_ctx, *frame.command_buffer, "DepthPrepass");
			const GpuScope scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, "Depth prepass");
			m_depth_prepass->record(*frame.command_buffer, m_current_frame);
		}
		frame.command_buffer.endRendering();
	}

	const bool voxels_drawn = recordVoxelScopes(frame, scene_viewport, scene_scissor, prepass_active);
	const vk::AttachmentLoadOp world_load = voxels_drawn ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear;
	scene_attachment_info.loadOp = world_load;
	normal_attachment_info.loadOp = world_load;

	vk::RenderingAttachmentInfo depth_attachment_info {};
	if (m_depth_resources.view.has_value()) {
		depth_attachment_info.imageView = **m_depth_resources.view;
		depth_attachment_info.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
		depth_attachment_info.loadOp = prepass_active || voxels_drawn ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear;
		depth_attachment_info.storeOp = vk::AttachmentStoreOp::eStore;
		depth_attachment_info.clearValue = clear_depth;
	}

	vk::RenderingAttachmentInfo indirect_attachment_info {};
	indirect_attachment_info.imageView = indirect_image != VK_NULL_HANDLE ? **m_scene_indirect.view : nullptr;
	indirect_attachment_info.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
	indirect_attachment_info.resolveMode = vk::ResolveModeFlagBits::eNone;
	indirect_attachment_info.loadOp = world_load;
	indirect_attachment_info.storeOp = vk::AttachmentStoreOp::eStore;
	indirect_attachment_info.clearValue = vk::ClearValue(vk::ClearColorValue(std::array {0.0f, 0.0f, 0.0f, 0.0f}));

	vk::RenderingAttachmentInfo motion_attachment_info {};
	motion_attachment_info.imageView = motion_image != VK_NULL_HANDLE ? **m_scene_motion.view : nullptr;
	motion_attachment_info.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
	motion_attachment_info.resolveMode = vk::ResolveModeFlagBits::eNone;
	motion_attachment_info.loadOp = world_load;
	motion_attachment_info.storeOp = vk::AttachmentStoreOp::eStore;
	motion_attachment_info.clearValue = vk::ClearValue(vk::ClearColorValue(std::array {0.0f, 0.0f, 0.0f, 0.0f}));

	const std::array scene_color_attachments {
	  scene_attachment_info, normal_attachment_info, indirect_attachment_info, motion_attachment_info
	};

	vk::RenderingInfo scene_rendering_info {};
	scene_rendering_info.renderArea = scene_scissor;
	scene_rendering_info.layerCount = 1;
	scene_rendering_info.colorAttachmentCount = static_cast<uint32_t>(scene_color_attachments.size());
	scene_rendering_info.pColorAttachments = scene_color_attachments.data();
	if (m_depth_resources.view.has_value()) {
		scene_rendering_info.pDepthAttachment = &depth_attachment_info;
	}
	frame.command_buffer.beginRendering(scene_rendering_info);
	frame.command_buffer.setViewport(0, std::array {scene_viewport});
	frame.command_buffer.setScissor(0, std::array {scene_scissor});

	const auto capture_glass_backdrop = [&] {
		if (!m_glass_backdrop.image || !m_glass_backdrop_depth.image || scene_image == VK_NULL_HANDLE || !m_depth_resources.image) {
			return;
		}
		const vk::CommandBuffer cmd = *frame.command_buffer;
		cmd.endRendering();

		sync::BarrierBatch to_copy;
		to_copy.image(m_scene_color.state, sync::Usage::transfer_src);
		to_copy.image(m_depth_resources.state, sync::Usage::transfer_src);
		to_copy.image(m_glass_backdrop.state, sync::Usage::transfer_dst);
		to_copy.image(m_glass_backdrop_depth.state, sync::Usage::transfer_dst);
		to_copy.flush(cmd);

		const vk::Extent3D copy_extent {render_extent.width, render_extent.height, 1};
		const vk::ImageSubresourceLayers color_layers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
		const vk::ImageSubresourceLayers depth_layers(vk::ImageAspectFlagBits::eDepth, 0, 0, 1);
		cmd.copyImage(
		    scene_image,
		    vk::ImageLayout::eTransferSrcOptimal,
		    **m_glass_backdrop.image,
		    vk::ImageLayout::eTransferDstOptimal,
		    vk::ImageCopy(color_layers, {}, color_layers, {}, copy_extent)
		);
		cmd.copyImage(
		    **m_depth_resources.image,
		    vk::ImageLayout::eTransferSrcOptimal,
		    **m_glass_backdrop_depth.image,
		    vk::ImageLayout::eTransferDstOptimal,
		    vk::ImageCopy(depth_layers, {}, depth_layers, {}, copy_extent)
		);

		sync::BarrierBatch resume;
		resume.image(m_scene_color.state, sync::Usage::color_attachment);
		resume.image(m_depth_resources.state, sync::Usage::depth_attachment);
		resume.image(m_glass_backdrop.state, sync::Usage::fragment_sampled);
		resume.image(m_glass_backdrop_depth.state, sync::Usage::depth_sampled);
		resume.flush(cmd);

		std::array resume_colors = scene_color_attachments;
		for (auto& attachment : resume_colors) {
			attachment.loadOp = vk::AttachmentLoadOp::eLoad;
		}
		vk::RenderingAttachmentInfo resume_depth = depth_attachment_info;
		resume_depth.loadOp = vk::AttachmentLoadOp::eLoad;
		vk::RenderingInfo resume_info = scene_rendering_info;
		resume_info.pColorAttachments = resume_colors.data();
		if (m_depth_resources.view.has_value()) {
			resume_info.pDepthAttachment = &resume_depth;
		}
		cmd.beginRendering(resume_info);
		cmd.setViewport(0, std::array {scene_viewport});
		cmd.setScissor(0, std::array {scene_scissor});
	};

	{
		TracyVkZone(m_tracy_vk_ctx, *frame.command_buffer, "Scene");
		const GpuScope scene_scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, "Scene");
		std::lock_guard lock(m_pass_mutex);

		const auto record_stage = [&](RenderStage stage) {
			for (auto& pass : m_render_passes) {
				// recordMeshScene draws the sky
				if (!pass->isEnabled() || pass->stage() != stage || pass.get() == m_environment_pass) {
					continue;
				}
				TracyVkZone(m_tracy_vk_ctx, *frame.command_buffer, "WorldPass");
				const GpuScope scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, pass->name());
				pass->record(*frame.command_buffer, m_current_frame, image_index);
			}
		};

		recordMeshScene(*frame.command_buffer, image_index, voxels_drawn, capture_glass_backdrop);
		record_stage(RenderStage::world);
	}

	frame.command_buffer.endRendering();

	// PROBE and Iradiance capture

	if (const auto* capture_frame = renderingFrame(); capture_frame != nullptr && capture_frame->probe_capture_index >= 0 &&
	                                                  m_reflection_probe_pass != nullptr && scene_image != VK_NULL_HANDLE) {
		sync::transition(*frame.command_buffer, m_scene_color.state, sync::Usage::transfer_src);

		m_reflection_probe_pass->captureFace(
		    *frame.command_buffer,
		    scene_image,
		    scene_extent,
		    static_cast<uint32_t>(capture_frame->probe_capture_index),
		    capture_frame->probe_capture_face
		);

		sync::transition(*frame.command_buffer, m_scene_color.state, sync::Usage::color_attachment);
	}

	if (const auto* capture_frame = renderingFrame(); capture_frame != nullptr && capture_frame->irradiance_capture_index >= 0 &&
	                                                  m_reflection_probe_pass != nullptr && scene_image != VK_NULL_HANDLE) {
		sync::transition(*frame.command_buffer, m_scene_color.state, sync::Usage::transfer_src);

		m_reflection_probe_pass->captureIrradianceFace(
		    *frame.command_buffer, scene_image, scene_extent, capture_frame->irradiance_capture_face
		);

		sync::transition(*frame.command_buffer, m_scene_color.state, sync::Usage::color_attachment);

		if (capture_frame->irradiance_capture_face == 5) {
			m_reflection_probe_pass->projectStagingToSh(
			    *frame.command_buffer, static_cast<uint32_t>(capture_frame->irradiance_capture_index)
			);
		}
	}

	// Auto exposure reads these from compute too
	barriers.image(m_scene_color.state, sync::Usage::shader_sampled);
	barriers.image(m_scene_normal.state, sync::Usage::shader_sampled);
	barriers.image(m_scene_indirect.state, sync::Usage::shader_sampled);
	barriers.image(m_scene_motion.state, sync::Usage::shader_sampled);
	barriers.image(m_depth_resources.state, sync::Usage::depth_sampled);
	if (m_traced_specular_frame) {
		barriers.image(m_voxel_albedo.state, sync::Usage::fragment_sampled);
		barriers.image(m_voxel_material.state, sync::Usage::fragment_sampled);
		barriers.image(m_voxel_face_normal.state, sync::Usage::fragment_sampled);
	}
	barriers.flush(*frame.command_buffer);

	const auto* capture_check = renderingFrame();
	const bool capture_frame =
	    capture_check != nullptr && (capture_check->probe_capture_index >= 0 || capture_check->irradiance_capture_index >= 0);

	vk::ImageView post_source = getSceneColorView();
	if (!capture_frame) {
		std::lock_guard lock(m_pass_mutex);
		const GpuScope post_scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, "Post process");
		for (auto& pass : m_post_process_passes) {
			if (!pass->isEnabled() || !post_source) {
				continue;
			}
			const GpuScope scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, pass->name());
			post_source = pass->record(*frame.command_buffer, m_current_frame, post_source);
		}
	}

	const bool upscaled_depth = m_output_depth.view.has_value() && m_depth_upscale_pipeline.isReady() &&
	                            m_depth_resources.view.has_value() && m_current_frame < m_depth_upscale_sets.size();
	if (upscaled_depth && !capture_frame) {
		const GpuScope scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, "Depth upscale");
		recordDepthUpscale(*frame.command_buffer, scissor);
	}
	DepthResources& overlay_depth = upscaled_depth ? m_output_depth : m_depth_resources;

	if (!capture_frame && m_selection_outline != nullptr && overlay_depth.view.has_value()) {
		const GpuScope scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, "Selection mask");
		m_selection_outline->recordMask(*frame.command_buffer, m_current_frame, **overlay_depth.view);
	}

	// The overlay scope depth tests against the scene and its DontCare store counts as a write
	barriers.image(overlay_depth.state, sync::Usage::depth_attachment);

	const vk::Image image = m_output_target->getColorImage(image_index);
	sync::Usage& output_usage = m_output_image_usage.at(image_index);
	barriers.image(image, colorSubresourceRange(), output_usage, sync::Usage::color_attachment);
	output_usage = sync::Usage::color_attachment;
	barriers.flush(*frame.command_buffer);

	vk::RenderingAttachmentInfo output_attachment_info {};
	output_attachment_info.imageView = *m_output_target->getColorAttachment(image_index);
	output_attachment_info.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
	output_attachment_info.resolveMode = vk::ResolveModeFlagBits::eNone;
	output_attachment_info.loadOp = vk::AttachmentLoadOp::eClear;
	output_attachment_info.storeOp = vk::AttachmentStoreOp::eStore;
	output_attachment_info.clearValue = clear_color;

	vk::RenderingAttachmentInfo overlay_depth_info {};
	if (overlay_depth.view.has_value()) {
		overlay_depth_info.imageView = **overlay_depth.view;
		overlay_depth_info.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
		overlay_depth_info.loadOp = vk::AttachmentLoadOp::eLoad;
		overlay_depth_info.storeOp = vk::AttachmentStoreOp::eDontCare;
	}

	vk::RenderingInfo output_rendering_info {};
	output_rendering_info.renderArea = scissor;
	output_rendering_info.layerCount = 1;
	output_rendering_info.colorAttachmentCount = 1;
	output_rendering_info.pColorAttachments = &output_attachment_info;
	if (overlay_depth.view.has_value()) {
		output_rendering_info.pDepthAttachment = &overlay_depth_info;
	}
	frame.command_buffer.beginRendering(output_rendering_info);
	frame.command_buffer.setViewport(0, std::array {viewport});
	frame.command_buffer.setScissor(0, std::array {scissor});

	{
		TracyVkZone(m_tracy_vk_ctx, *frame.command_buffer, "Present");
		const GpuScope scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, "Present blit");

		if (m_present_pipeline.isReady() && post_source && m_current_frame < m_present_sets.size()) {
			if (m_present_bound_views[m_current_frame] != post_source) {
				vk::DescriptorImageInfo image_info {};
				image_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
				image_info.imageView = post_source;
				image_info.sampler = *m_present_sampler;

				const vk::WriteDescriptorSet write(
				    *m_present_sets[m_current_frame], 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &image_info
				);
				m_core->getDevice().updateDescriptorSets(write, {});
				m_present_bound_views[m_current_frame] = post_source;
			}

			frame.command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_present_pipeline.getPipeline());
			frame.command_buffer.bindDescriptorSets(
			    vk::PipelineBindPoint::eGraphics,
			    *m_present_layout.getPipelineLayout(),
			    0,
			    std::array<vk::DescriptorSet, 1> {*m_present_sets[m_current_frame]},
			    {}
			);
			frame.command_buffer.draw(3, 1, 0, 0);
		}
	}

	if (!capture_frame && m_selection_outline != nullptr) {
		const GpuScope scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, "Selection outline");
		m_selection_outline->recordOutline(*frame.command_buffer, m_current_frame);
	}

	if (!capture_frame) {
		std::lock_guard lock(m_pass_mutex);
		const GpuScope overlay_scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, "Overlays");
		for (auto& pass : m_render_passes) {
			if (!pass->isEnabled() || pass->stage() != RenderStage::overlay) {
				continue;
			}
			TracyVkZone(m_tracy_vk_ctx, *frame.command_buffer, "OverlayPass");
			const GpuScope scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, pass->name());
			pass->record(*frame.command_buffer, m_current_frame, image_index);
		}
	}

	frame.command_buffer.endRendering();

	{
		const GpuScope scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, "Output finalize");
		m_output_target->recordFinalize(frame.command_buffer, image_index);
	}
	// Must match the last transition IOutputTarget::recordFinalize records
	output_usage = m_output_target->usesAcquirePresentSemaphores() ? sync::Usage::present : sync::Usage::transfer_src;

	TracyVkCollect(m_tracy_vk_ctx, *frame.command_buffer);

#ifdef TRACY_ENABLE
	if (m_shadow_pass != nullptr) {
		const ShadowPass::Stats& shadow = m_shadow_pass->getStats();
		TracyPlot("Shadow: draws", static_cast<int64_t>(shadow.draws));
		TracyPlot("Shadow: scopes", static_cast<int64_t>(shadow.scopes));
		TracyPlot("Shadow: groups composed", static_cast<int64_t>(shadow.composed));
		TracyPlot("Shadow: groups cached", static_cast<int64_t>(shadow.cached));
		TracyPlot("Shadow: static for view move", static_cast<int64_t>(shadow.static_views));
		TracyPlot("Shadow: static for casters", static_cast<int64_t>(shadow.static_casters));
		TracyPlot("Shadow: static for voxel edits", static_cast<int64_t>(shadow.static_edits));
		TracyPlot("Shadow: moving volumes", static_cast<int64_t>(shadow.moving_volumes));
		TracyPlot("Shadow: voxel draws marched", static_cast<int64_t>(shadow.marched_volumes));
		TracyPlot("Shadow: voxel draws meshed", static_cast<int64_t>(shadow.meshed_volumes));
	}
#endif

	if (m_gpu_timer != nullptr) {
		m_gpu_timer->endGraphics(m_current_frame, *frame.command_buffer);
	}
	frame.command_buffer.end();
}

void VulkanRenderer::ensureMaterialPasses(RenderFrame& frame_data) {
	ZoneScoped;

	std::lock_guard lock(m_pass_mutex);
	if (m_pending_material_pass_clear.exchange(false, std::memory_order_acq_rel)) {
		for (auto& [material, pass] : m_material_passes) {
			retire(std::shared_ptr<MaterialPass>(std::move(pass)));
		}
		m_material_passes.clear();
	}

	for (const auto& proxy : frame_data.mesh_instances) {
		if (proxy.root_material == nullptr || m_material_passes.contains(proxy.root_material)) {
			continue;
		}
		auto pass = std::make_unique<MaterialPass>(
		    *m_core, proxy.root_material, m_scene_color_format, m_depth_format, m_output_target->getExtent()
		);
		TOAST_INFO("Render", "Created material pass '{}'", pass->name());
		m_material_passes.emplace(proxy.root_material, std::move(pass));
	}

	// A baked probe must not miss a material that is still building
	if (frame_data.probe_capture_index >= 0 || frame_data.irradiance_capture_index >= 0) {
		for (auto& [material, pass] : m_material_passes) {
			pass->finishPendingBuild();
		}
	}
}

void VulkanRenderer::hideInstancesWithoutPipeline(RenderFrame& frame_data) {
	ZoneScoped;
	std::lock_guard lock(m_pass_mutex);
	for (auto& proxy : frame_data.mesh_instances) {
		if (!proxy.visible || proxy.root_material == nullptr) {
			continue;
		}
		const auto it = m_material_passes.find(proxy.root_material);
		if (it == m_material_passes.end() || !it->second->isValid()) {
			proxy.visible = false;
		}
	}
}

void VulkanRenderer::updateFrameResources(uint32_t frame_index, RenderFrame& frame_data) {
	ZoneScoped;
	m_frame_ubos[frame_index] = frame_data.frame_data;

	const auto& allocation = m_frame_ubo_res[frame_index].gpu_buffer->getAllocation();
	auto* mapped = allocation.getInfo().pMappedData;

	if (mapped) {
		std::memcpy(mapped, &m_frame_ubos[frame_index], sizeof(FrameUBO));

		allocation.flush(0, sizeof(FrameUBO));
	}

	if (frame_index < m_joint_matrix_res.size() && m_joint_matrix_res[frame_index].gpu_buffer.has_value() &&
	    !frame_data.joint_matrices.empty()) {
		const auto count = std::min<size_t>(frame_data.joint_matrices.size(), k_max_joint_matrices);
		const auto bytes = static_cast<vk::DeviceSize>(count) * sizeof(glm::mat4);

		const auto& joint_allocation = m_joint_matrix_res[frame_index].gpu_buffer->getAllocation();
		if (auto* joint_mapped = joint_allocation.getInfo().pMappedData) {
			std::memcpy(joint_mapped, frame_data.joint_matrices.data(), bytes);
			joint_allocation.flush(0, bytes);
		}
	}

	const auto upload_instances =
	    [](const std::vector<FrameResources>& target, uint32_t index, const std::vector<InstanceData>& src) {
		    if (index >= target.size() || !target[index].gpu_buffer.has_value() || src.empty()) {
			    return;
		    }
		    const auto count = std::min<size_t>(src.size(), k_max_instances);
		    const auto bytes = static_cast<vk::DeviceSize>(count) * sizeof(InstanceData);

		    const auto& allocation = target[index].gpu_buffer->getAllocation();
		    if (auto* mapped = allocation.getInfo().pMappedData) {
			    std::memcpy(mapped, src.data(), bytes);
			    allocation.flush(0, bytes);
		    }
	    };

	upload_instances(m_instance_res, frame_index, frame_data.instance_data);
	upload_instances(m_shadow_instance_res, frame_index, frame_data.shadow_instance_data);

	if (frame_index < m_fog_volume_res.size() && m_fog_volume_res[frame_index].gpu_buffer.has_value() &&
	    !frame_data.fog_volumes.empty()) {
		const auto count = std::min<size_t>(frame_data.fog_volumes.size(), k_max_fog_volumes);
		const auto bytes = static_cast<vk::DeviceSize>(count) * sizeof(FogVolumeGpu);
		const auto& fog_allocation = m_fog_volume_res[frame_index].gpu_buffer->getAllocation();
		if (auto* fog_mapped = fog_allocation.getInfo().pMappedData) {
			std::memcpy(fog_mapped, frame_data.fog_volumes.data(), bytes);
			fog_allocation.flush(0, bytes);
		}
	}
}

auto VulkanRenderer::drawFrame(RenderFrame& frame_data) -> bool {
	ZoneScoped;
	if (m_frames.empty()) {
		return false;
	}

	if (!m_output_target->isPresentable()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		return false;
	}

	using clock = std::chrono::steady_clock;
	auto phase_start = clock::now();
	auto end_phase = [&phase_start](std::atomic<uint64_t>& counter) {
		const auto now = clock::now();
		counter.fetch_add(
		    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now - phase_start).count()),
		    std::memory_order_relaxed
		);
		phase_start = now;
	};

	toast::PhaseZones phases;
	TOAST_PHASE(phases, "Draw: uploads");
	processPendingUploads();

	flushResourceUploads();
	end_phase(m_perf_upload_ns);

	auto& frame = m_frames[m_current_frame];
	TOAST_PHASE(phases, "Draw: wait frame fence");
	std::ignore = m_core->getDevice().waitForFences(*frame.in_flight, true, std::numeric_limits<uint64_t>::max());
	end_phase(m_perf_gpu_wait_ns);
	TOAST_PHASE(phases, "Draw: publish frames and timings");
	frame.retired.clear();
	publishCompletedFrames();
	m_core->updateTracyMemory();

	if (m_gpu_timer != nullptr && !frame.was_probe_capture) {
		m_gpu_timer->collect(m_current_frame);
#if defined(TOAST_DEV_TOOLS)
		reportGpuSpike(*m_gpu_timer);
#endif
#ifdef TRACY_ENABLE
		plotGpuTimings(*m_gpu_timer);
#endif
	}
	end_phase(m_perf_record_ns);

	TOAST_PHASE(phases, "Draw: acquire image");
	auto acquired = m_output_target->acquireNextImage(std::numeric_limits<uint64_t>::max(), *frame.image_available, VK_NULL_HANDLE);
	// A dropped snapshot breaks every temporal history so a busy compositor is waited on and only a stalled one drops it
	for (uint32_t attempt = 0; acquired.result == vk::Result::eNotReady && attempt < k_output_acquire_attempts &&
	                           m_running.load(std::memory_order_acquire);
	     ++attempt) {
		waitForOutputImage();
		acquired = m_output_target->acquireNextImage(std::numeric_limits<uint64_t>::max(), *frame.image_available, VK_NULL_HANDLE);
	}
	end_phase(m_perf_acquire_ns);

	if (acquired.result == vk::Result::eErrorOutOfDateKHR) {
		TOAST_WARN("Render", "Swapchain out of date on acquire; recreating");
		applyResize(m_output_target->getExtent());
		return false;
	}
	if (acquired.result == vk::Result::eNotReady) {
		TOAST_WARN("Render", "Output image wait timed out so the snapshot was dropped");
		return false;
	}
	if (acquired.result != vk::Result::eSuccess && acquired.result != vk::Result::eSuboptimalKHR) {
		TOAST_CRITICAL("Render", "Toast Engine Error: Failed to acquire the next output image!");
	}

	const uint32_t image_index = acquired.value;
	TOAST_PHASE(phases, "Draw: wait image fence");
	// The acquire semaphore and the queue order image reuse and a CPU wait here pinned composed HDR to one frame
	if (!m_output_target->usesAcquirePresentSemaphores() && m_images_in_flight.at(image_index)) {
		std::ignore =
		    m_core->getDevice().waitForFences(m_images_in_flight.at(image_index), true, std::numeric_limits<uint64_t>::max());
	}
	end_phase(m_perf_gpu_wait_ns);

	m_core->getDevice().resetFences(*frame.in_flight);
	m_images_in_flight[image_index] = *frame.in_flight;

	TOAST_PHASE(phases, "Draw: update frame resources");
	updateFrameResources(m_current_frame, frame_data);    // FIXME dt

	m_rendering_frame = &frame_data;
	TOAST_PHASE(phases, "Draw: update passes");
	ensureMaterialPasses(frame_data);

	// TODO move outside the render loop
	bool any_unbuilt_material = false;
	{
		std::lock_guard lock(m_pass_mutex);
		for (auto& [material, pass] : m_material_passes) {
			pass->update(m_current_frame, Time::renderDelta());
			any_unbuilt_material = any_unbuilt_material || !pass->isValid();
		}
		for (auto& pass : m_render_passes) {
			pass->update(m_current_frame, Time::renderDelta());
		}
		// Before recordFrame dispatches them
		for (auto& pass : m_compute_passes) {
			pass->update(m_current_frame, Time::renderDelta());
		}
	}
	// Only while a first build runs so the steady state skips the walk
	if (any_unbuilt_material) {
		hideInstancesWithoutPipeline(frame_data);
	}

	if (const auto* rendering = renderingFrame(); rendering != nullptr) {
		frame.was_probe_capture = rendering->probe_capture_index >= 0 || rendering->irradiance_capture_index >= 0;
	} else {
		frame.was_probe_capture = false;
	}

	TOAST_PHASE(phases, "Draw: record frame");
	recordFrame(frame, image_index);
	end_phase(m_perf_record_ns);

	TOAST_PHASE(phases, "Draw: submit setup");
	const bool present_sync = m_output_target->usesAcquirePresentSemaphores();

	const vk::CommandBuffer command_buffer = *frame.command_buffer;
	const vk::Semaphore signal_semaphore = *m_render_finished_per_image.at(image_index);
	const vk::Semaphore wait_semaphore = *frame.image_available;

	// AsyncCompute work never synchronises with a frame on the GPU
	std::array<sync::SemaphoreWait, 3> waits {};
	uint32_t wait_count = 0;
	if (present_sync) {
		waits[wait_count++] = {wait_semaphore, vk::PipelineStageFlagBits2::eColorAttachmentOutput};
	}
	// Already reached but makes the release to acquire order explicit for validation
	if (m_upload_acquired_value > m_upload_waited_value) {
		waits[wait_count++] = {*m_upload_timeline, vk::PipelineStageFlagBits2::eAllCommands, m_upload_acquired_value};
		m_upload_waited_value = m_upload_acquired_value;
	}
	const sync::SemaphoreSignal present_signal {signal_semaphore};

	const OutputSubmitSync output_sync = m_output_target->submitSync(image_index);
	if (output_sync.wait) {
		waits[wait_count++] = {output_sync.wait, vk::PipelineStageFlags2(static_cast<VkPipelineStageFlags>(output_sync.wait_stage))};
	}
	std::array<sync::SemaphoreSignal, 2> signals {};
	uint32_t signal_count = 0;
	if (present_sync) {
		signals[signal_count++] = present_signal;
	}
	if (output_sync.signal) {
		signals[signal_count++] = {output_sync.signal};
	}

	sync::SubmitDesc submit {};
	submit.commands = {&command_buffer, 1};
	submit.waits = std::span(waits).first(wait_count);
	submit.signals = std::span(signals).first(signal_count);
	submit.next = output_sync.chain;
	submit.fence = *frame.in_flight;

	vk::FrameBoundaryEXT frame_boundary {};
	if (m_core->isFrameBoundarySupported()) {
		frame_boundary.flags = vk::FrameBoundaryFlagBitsEXT::eFrameEnd;
		frame_boundary.frameID = m_frame_boundary_counter++;
		frame_boundary.pNext = output_sync.chain;
		submit.next = &frame_boundary;
	}

	TOAST_PHASE(phases, "Draw: submit");
	{
		std::scoped_lock submit_lock(m_core->graphicsSubmitMutex());
		sync::submit(m_core->getGraphicsQueue(), submit);
	}
	// Steps the tick queued run on the compute queue while this frame renders
	m_core->asyncCompute().flushDeferred();
	end_phase(m_perf_submit_ns);

#if defined(_WIN32) && defined(TOAST_DEV_TOOLS)
	if (m_core->getNsightMode() == NsightMode::graphics_capture) {
		NGFX_ResourceDescription_Vulkan output_resource {NGFX_ResourceDescription_Vulkan_VER};
		output_resource.type = NGFX_ResourceType_Vulkan_VkImage;
		output_resource.image = static_cast<VkImage>(m_output_target->getColorImage(image_index));

		NGFX_FrameBoundary_Vulkan_Params boundary_params {NGFX_FrameBoundary_Vulkan_Params_VER};
		boundary_params.queue = static_cast<VkQueue>(m_core->getGraphicsQueue());
		boundary_params.outputResources = &output_resource;
		boundary_params.numOutputResources = 1;
		NGFX_FrameBoundary_Vulkan(&boundary_params);
	}
#endif

	TOAST_PHASE(phases, "Draw: present");
	const auto present_result = m_output_target->present(image_index, present_sync ? signal_semaphore : vk::Semaphore {});
	end_phase(m_perf_present_ns);
	frame.last_image_index = image_index;
	frame.has_submitted = true;
	m_pending_publish.push_back(m_current_frame);

	m_current_frame = (m_current_frame + 1) % static_cast<uint32_t>(m_frames.size());

	if (present_result == vk::Result::eErrorOutOfDateKHR || present_result == vk::Result::eSuboptimalKHR) {
		TOAST_WARN("Render", "Swapchain out of date or suboptimal on present; recreating");
		m_rendering_frame = nullptr;
		applyResize(m_output_target->getExtent());
		return true;
	}
	if (present_result != vk::Result::eSuccess) {
		TOAST_CRITICAL("Render", "Toast Engine Error: Failed to present the current output image!");
	}

	m_rendering_frame = nullptr;
	return true;
}

}
