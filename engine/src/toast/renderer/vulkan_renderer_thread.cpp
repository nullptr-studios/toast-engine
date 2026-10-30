/// @file vulkan_renderer_thread.cpp
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

void VulkanRenderer::waitForOutputImage() {
	ZoneScoped;
	using clock = std::chrono::steady_clock;
	constexpr auto k_budget = std::chrono::milliseconds(8);
	constexpr auto k_step = std::chrono::milliseconds(1);
	const auto deadline = clock::now() + k_budget;
	while (clock::now() < deadline) {
		// An image the GPU still renders only frees once this thread publishes it
		publishCompletedFrames();
		if (!m_pending_publish.empty()) {
			const auto& oldest = m_frames[m_pending_publish.front()];
			std::ignore = m_core->getDevice().waitForFences(
			    *oldest.in_flight, true, static_cast<uint64_t>(std::chrono::nanoseconds(k_step).count())
			);
			publishCompletedFrames();
		}
		if (m_output_target->waitForFreeImage(k_step)) {
			return;
		}
	}
}

void VulkanRenderer::mainRenderThread() try {
#ifdef TRACY_ENABLE
	tracy::SetThreadName("Renderer Thread");
#endif

#ifdef TRACY_ENABLE
	{
		const vk::CommandBufferAllocateInfo alloc_info(*m_command_pool, vk::CommandBufferLevel::ePrimary, 1);
		const auto tracy_cmd_buffers = m_core->getDevice().allocateCommandBuffers(alloc_info);
		m_tracy_vk_ctx =
		    TracyVkContext(*m_core->getPhysicalDevice(), *m_core->getDevice(), m_core->getGraphicsQueue(), *tracy_cmd_buffers[0]);
	}
#endif

	if (m_core != nullptr) {
		try {
			m_gpu_timer = std::make_unique<GpuTimer>(*m_core, static_cast<uint32_t>(m_frames.size()));
		} catch (const std::exception& e) {
			TOAST_WARN("Render", "GPU timing unavailable, the performance overlay shows CPU timings only: {}", e.what());
			m_gpu_timer.reset();
		}
	}

	using clock = std::chrono::steady_clock;
	auto next_frame_deadline = clock::now();

	RenderFrame frame_to_draw;
	bool has_frame = false;

	std::array<float, 60> frame_time_window {};
	size_t frame_time_cursor = 0;
	clock::time_point last_frame_time {};

	while (m_running.load(std::memory_order_acquire)) {
		ZoneScopedN("VulkanRenderer::mainRenderThread");

		if (m_rendering_paused.load(std::memory_order_acquire)) {
			std::unique_lock lock(m_queue_mutex);
			m_frame_cv.wait(lock, [this] {
				return !m_rendering_paused.load(std::memory_order_acquire) || !m_running.load(std::memory_order_acquire);
			});
			continue;
		}

		const uint64_t pending_resize = m_pending_resize_packed.exchange(k_no_pending_resize, std::memory_order_acq_rel);
		if (pending_resize != k_no_pending_resize) {
			const auto extent = detail::unpackExtent(pending_resize);
			if (extent.width > 0 && extent.height > 0) {
				applyResizeInternal(extent);
			}
		}

		bool consumed_queued_frame = false;

		const double limit_hz = m_bake_active.load(std::memory_order_relaxed) ? 0.0 : effectiveFrameRateLimit();
		const bool clamp_to_simulation = m_clamp_to_simulation.load(std::memory_order_relaxed);

		{
			std::unique_lock lock(m_queue_mutex);

			auto wake_condition = [this] {
				return !m_ready_frames.empty() || !m_running ||
				       m_pending_resize_packed.load(std::memory_order_acquire) != k_no_pending_resize;
			};

			const auto frame_wait_start = clock::now();
			if (m_ready_frames.empty()) {
				if (!has_frame || clamp_to_simulation) {
					m_frame_cv.wait(lock, wake_condition);
				} else if (limit_hz > 0.0) {
					const auto now = clock::now();
					if (next_frame_deadline > now) {
						m_frame_cv.wait_for(lock, next_frame_deadline - now, wake_condition);
					}
				}
			}
			m_perf_frame_wait_ns.fetch_add(
			    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - frame_wait_start).count()),
			    std::memory_order_relaxed
			);

			if (!m_running) {
				return;
			}

			if (!m_ready_frames.empty()) {
				const auto frame_index = m_ready_frames.front();
				m_ready_frames.pop();

				std::swap(frame_to_draw, m_render_frames[frame_index]);
				has_frame = true;
				consumed_queued_frame = true;

				const uint64_t previous = m_last_drawn_sequence.exchange(frame_to_draw.sequence, std::memory_order_relaxed);
				if (frame_to_draw.sequence < previous) {
					m_out_of_order_frames.fetch_add(1, std::memory_order_relaxed);
					TOAST_WARN(
					    "VulkanRenderer",
					    "Frame {} drawn after frame {} - render frames arrived out of order",
					    frame_to_draw.sequence,
					    previous
					);
				} else if (previous != 0 && frame_to_draw.sequence > previous + 1) {
					m_dropped_frames.fetch_add(static_cast<uint32_t>(frame_to_draw.sequence - previous - 1), std::memory_order_relaxed);
				}
			} else if (!has_frame) {
				continue;
			}
		}

		// Clamped frames are capped in waitForFrameSlot
		if (limit_hz > 0.0 && !clamp_to_simulation) {
			const auto pacing_start = clock::now();
			std::this_thread::sleep_until(next_frame_deadline);
			m_perf_pacing_ns.fetch_add(
			    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - pacing_start).count()),
			    std::memory_order_relaxed
			);
			const auto interval = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / limit_hz));
			const auto now = clock::now();
			next_frame_deadline = (next_frame_deadline + interval > now) ? next_frame_deadline + interval : now + interval;
		}

		publishCompletedFrames();

		Time::get().renderTick();

		const bool do_capture = m_capture_frame_requested.exchange(false, std::memory_order_acq_rel);
		const auto* rdoc_api = m_core->getRenderDocAPI();
		const auto nsight_mode = m_core->getNsightMode();

		if (do_capture && rdoc_api != nullptr) {
			rdoc_api->StartFrameCapture(nullptr, nullptr);
		}
#if defined(_WIN32) && defined(TOAST_DEV_TOOLS)
		else if (do_capture && nsight_mode == NsightMode::graphics_capture) {
			NGFX_GraphicsCapture_StartCapture_Vulkan_Params params {NGFX_GraphicsCapture_StartCapture_Vulkan_Params_VER};
			const NGFX_Result start_result = NGFX_GraphicsCapture_StartCapture_Vulkan(&params);
			TOAST_INFO("VulkanRenderer", "NGFX_GraphicsCapture_StartCapture_Vulkan result={}", static_cast<int>(start_result));
		} else if (do_capture && nsight_mode == NsightMode::gpu_trace) {
			m_core->activateNsightGpuTraceIfNeeded();
			NGFX_GPUTrace_StartTrace_Vulkan_Params params {NGFX_GPUTrace_StartTrace_Vulkan_Params_VER};
			const NGFX_Result start_result = NGFX_GPUTrace_StartTrace_Vulkan(&params);
			TOAST_INFO("VulkanRenderer", "NGFX_GPUTrace_StartTrace_Vulkan result={}", static_cast<int>(start_result));
		} else if (do_capture) {
			TOAST_WARN(
			    "VulkanRenderer",
			    "Capture requested but no graphics debugger is attached - RenderDoc was not found in this process and "
			    "no Nsight activity initialized (see the VulkanCore log lines at startup)"
			);
		}
#endif

		const auto draw_start = clock::now();
		// A pass with no free output image rendered nothing
		if (drawFrame(frame_to_draw)) {
			m_perf_draw_work_ns.fetch_add(
			    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - draw_start).count()),
			    std::memory_order_relaxed
			);
			m_perf_draws.fetch_add(1, std::memory_order_relaxed);
			if (!consumed_queued_frame) {
				m_perf_repeated_draws.fetch_add(1, std::memory_order_relaxed);
			}

			{
				static constexpr size_t k_window = 60;
				const auto now = clock::now();
				if (last_frame_time.time_since_epoch().count() != 0) {
					const auto interval_ms = std::chrono::duration<float, std::milli>(now - last_frame_time).count();
					frame_time_window[frame_time_cursor % k_window] = interval_ms;
					++frame_time_cursor;

					const size_t filled = std::min<size_t>(frame_time_cursor, k_window);
					float sum = 0.0f;
					float lowest = std::numeric_limits<float>::max();
					float highest = 0.0f;
					for (size_t i = 0; i < filled; ++i) {
						sum += frame_time_window[i];
						lowest = std::min(lowest, frame_time_window[i]);
						highest = std::max(highest, frame_time_window[i]);
					}
					m_frame_time_avg_ms.store(sum / static_cast<float>(filled), std::memory_order_relaxed);
					m_frame_time_min_ms.store(lowest, std::memory_order_relaxed);
					m_frame_time_max_ms.store(highest, std::memory_order_relaxed);
				}
				last_frame_time = now;
			}
		}

		if (do_capture && rdoc_api != nullptr) {
			rdoc_api->EndFrameCapture(nullptr, nullptr);
			TOAST_INFO("VulkanRenderer", "RenderDoc frame capture triggered");
		}
#if defined(_WIN32) && defined(TOAST_DEV_TOOLS)
		else if (do_capture && nsight_mode == NsightMode::graphics_capture) {
			NGFX_GraphicsCapture_StopCapture_Vulkan_Params params {NGFX_GraphicsCapture_StopCapture_Vulkan_Params_VER};
			const NGFX_Result stop_result = NGFX_GraphicsCapture_StopCapture_Vulkan(&params);
			TOAST_INFO("VulkanRenderer", "NGFX_GraphicsCapture_StopCapture_Vulkan result={}", static_cast<int>(stop_result));
		} else if (do_capture && nsight_mode == NsightMode::gpu_trace) {
			NGFX_GPUTrace_StopTrace_Vulkan_Params params {NGFX_GPUTrace_StopTrace_Vulkan_Params_VER};
			params.flags = NGFX_GPUTrace_StopTraceFlag_None;
			params.queue = m_core->getGraphicsQueue();
			params.outputResources = nullptr;
			params.numOutputResources = 0;
			const NGFX_Result stop_result = NGFX_GPUTrace_StopTrace_Vulkan(&params);
			TOAST_INFO("VulkanRenderer", "NGFX_GPUTrace_StopTrace_Vulkan result={}", static_cast<int>(stop_result));
		}
#endif

		if (consumed_queued_frame) {
			m_free_frames.release();
		}

#ifdef TRACY_ENABLE
		{
			const auto memory_props = m_core->getPhysicalDevice().getMemoryProperties();
			const auto budgets = m_core->getAllocator().getHeapBudgets();
			uint64_t vram_used = 0;
			uint64_t vram_budget = 0;
			for (uint32_t i = 0; i < memory_props.memoryHeapCount && i < budgets.size(); ++i) {
				if (memory_props.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal) {
					vram_used += budgets[i].usage;
					vram_budget += budgets[i].budget;
				}
			}
			TracyPlot("VRAM used", static_cast<int64_t>(vram_used));
			TracyPlot("VRAM budget", static_cast<int64_t>(vram_budget));
		}
#endif

		FrameMarkNamed("RenderFrame");
	}
} catch (const vk::DeviceLostError& error) {
	// Nothing recovers a lost device and the abort would otherwise drop the queued logs
	TOAST_CRITICAL("Render", "GPU device lost on the render thread, shutting down: {}", error.what());
	logging::flush();
	std::abort();
}

void VulkanRenderer::acquireFrameSlot() noexcept {
	using namespace std::chrono_literals;
	const auto slot_wait_start = std::chrono::steady_clock::now();
	m_frame_slot = m_free_frames.try_acquire_for(50ms) ? FrameSlot::held : FrameSlot::missed;
	m_perf_slot_wait_ns.fetch_add(
	    static_cast<uint64_t>(
	        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - slot_wait_start).count()
	    ),
	    std::memory_order_relaxed
	);
}

void VulkanRenderer::waitForFrameSlot() noexcept {
	ZoneScoped;
	using clock = std::chrono::steady_clock;

	const double limit_hz = m_bake_active.load(std::memory_order_relaxed) ? 0.0 : effectiveFrameRateLimit();
	if (limit_hz > 0.0) {
		std::this_thread::sleep_until(m_tick_deadline);
		const auto interval = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / limit_hz));
		const auto now = clock::now();
		m_tick_deadline = (m_tick_deadline + interval > now) ? m_tick_deadline + interval : now + interval;
	}

	// A paused tick keeps its slot
	if (m_frame_slot != FrameSlot::held && !m_rendering_paused.load(std::memory_order_relaxed)) {
		acquireFrameSlot();
	}
}

auto VulkanRenderer::previousResolvedColour() const noexcept -> vk::ImageView {
	const auto* frame = renderingFrame();
	if (m_taa_pass == nullptr || !m_taa_pass->isEnabled() || frame == nullptr || !frame->taa_active || frame->taa_reset) {
		return {};
	}
	return m_taa_pass->resolvedHistory();
}

void VulkanRenderer::submitFrame() noexcept {
	{
		std::lock_guard lock(m_queue_mutex);

		m_ready_frames.push(m_write_index);

		m_write_index = (m_write_index + 1) % k_render_frames;
	}

	m_frame_cv.notify_one();
}

}
