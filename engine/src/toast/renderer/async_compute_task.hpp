/// @file async_compute_task.hpp
/// @author dario
/// @date 10/10/2026

#pragma once

#include "async_compute.hpp"
#include "gpu_sync.hpp"
#include "shader_layout.hpp"
#include "vulkan_pipeline.hpp"
#include "vulkan_renderer.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <toast/export.hpp>
#include <vector>
#include <vulkan-memory-allocator-hpp/vk_mem_alloc_raii.hpp>

namespace renderer {

/// Base for a compute simulation that runs on the async compute queue and feeds the renderer
///
/// A derived class declares its buffers and bindings in its constructor and then calls step() from its own thread
///
/// Ring buffers are written once per step and read by the renderer and the CPU
/// Persistent buffers are state only the task touches, like velocities
class TOAST_API AsyncComputeTask {
public:
	using BufferId = uint32_t;

	/// Which copy of a ring buffer a binding sees during a step
	enum class Copy : uint8_t {
		/// Written by this step
		current,
		/// Result of the step before, read only
		previous,
	};

	struct Config {
		std::string shader_uri;
		std::vector<std::string> entries {"computeMain"};
		std::string debug_name = "AsyncComputeTask";
	};

	/// Handed to the step callback. Barriers between dispatches of one step are inserted for you
	class TOAST_API StepContext {
	public:
		template<typename Push>
		void dispatch(uint32_t groups_x, const Push& push, uint32_t entry = 0, uint32_t groups_y = 1, uint32_t groups_z = 1) {
			dispatchRaw(entry, {groups_x, groups_y, groups_z}, &push, sizeof(Push));
		}

		void dispatch(uint32_t groups_x, uint32_t entry = 0, uint32_t groups_y = 1, uint32_t groups_z = 1) {
			dispatchRaw(entry, {groups_x, groups_y, groups_z}, nullptr, 0);
		}

		/// For anything the helpers do not cover
		[[nodiscard]]
		auto commandBuffer() const -> vk::CommandBuffer {
			return m_cmd;
		}

		[[nodiscard]]
		auto hostData(BufferId id) const -> std::span<std::byte>;

		template<typename T>
		[[nodiscard]]
		auto hostData(BufferId id) const -> std::span<T> {
			const std::span<std::byte> bytes = hostData(id);
			return {reinterpret_cast<T*>(bytes.data()), bytes.size() / sizeof(T)};
		}

	private:
		friend class AsyncComputeTask;

		StepContext(AsyncComputeTask& task, vk::CommandBuffer cmd, uint32_t slot) : m_task(task), m_cmd(cmd), m_slot(slot) { }

		void dispatchRaw(uint32_t entry, std::array<uint32_t, 3> groups, const void* push, uint32_t push_size);

		AsyncComputeTask& m_task;
		vk::CommandBuffer m_cmd;
		uint32_t m_slot;
		bool m_dispatched = false;
	};

	/// Buffers the renderer may read this frame
	class FrameView {
	public:
		[[nodiscard]]
		explicit operator bool() const {
			return m_task != nullptr;
		}

		/// Null for a persistent buffer since only the task touches those
		[[nodiscard]]
		auto buffer(BufferId id) const -> vk::Buffer;

		/// Step counter of the data
		[[nodiscard]]
		auto step() const -> uint64_t {
			return m_step;
		}

	private:
		friend class AsyncComputeTask;
		const AsyncComputeTask* m_task = nullptr;
		uint32_t m_slot = 0;
		uint64_t m_step = 0;
	};

	virtual ~AsyncComputeTask();

	AsyncComputeTask(const AsyncComputeTask&) = delete;
	auto operator=(const AsyncComputeTask&) -> AsyncComputeTask& = delete;
	AsyncComputeTask(AsyncComputeTask&&) = delete;
	auto operator=(AsyncComputeTask&&) -> AsyncComputeTask& = delete;

	/// Render thread after the fence of frame_index was waited on, empty until a step finished
	/// next is how the renderer reads the ring buffers
	auto acquireForFrame(vk::CommandBuffer cmd, uint32_t frame_index, sync::Usage next) -> FrameView;

	/// Newest finished CPU copy of a ring buffer added with cpu_readback and false until one exists
	auto read(BufferId id, std::span<std::byte> out) -> bool;

	template<typename T>
	auto read(BufferId id, std::span<T> out) -> bool {
		return read(id, std::as_writable_bytes(out));
	}

	/// Steps finished so far
	[[nodiscard]]
	auto finishedStep() const -> uint64_t;

	[[nodiscard]]
	auto isReady() const -> bool {
		return m_ready;
	}

protected:
	explicit AsyncComputeTask(Config config);

	/// Constructor only
	auto addRingBuffer(vk::DeviceSize size, bool cpu_readback = false) -> BufferId;
	auto addPersistentBuffer(vk::DeviceSize size) -> BufferId;
	auto addHostBuffer(vk::DeviceSize size) -> BufferId;
	void bind(uint32_t binding, BufferId id, Copy copy = Copy::current);
	void bindExternal(uint32_t set, uint32_t binding, std::function<vk::Buffer()> source);
	void setAcquireUsage(BufferId id, sync::Usage usage);

	/// Owner thread, blocks until the copy landed gpu side
	void upload(BufferId id, std::span<const std::byte> data);

	template<typename T>
	void upload(BufferId id, std::span<const T> data) {
		upload(id, std::as_bytes(data));
	}

	/// Owner thread
	auto step(
	    const std::function<void(StepContext&)>& record, std::string_view debug_name = {},
	    std::shared_ptr<const void> keep_alive = {}
	) -> uint64_t;

private:
	/// Frames in flight plus the step input plus the newest result plus the one being written
	static constexpr uint32_t k_slot_count = VulkanRenderer::k_frames_in_flight + 3;

	struct Buffer {
		bool ring = false;
		bool host = false;
		bool cpu_readback = false;
		vk::DeviceSize size = 0;
		/// One per slot for a ring buffer and a single entry otherwise
		std::vector<vma::raii::Buffer> copies;
		std::vector<vma::raii::Buffer> readbacks;
		std::optional<sync::Usage> acquire_usage;
	};

	struct Binding {
		uint32_t binding = 0;
		BufferId id = 0;
		Copy copy = Copy::current;
	};

	struct ExternalBinding {
		uint32_t set = 0;
		uint32_t binding = 0;
		std::function<vk::Buffer()> source;
	};

	struct Slot {
		std::vector<vk::raii::DescriptorSet> sets;
		AsyncCompute::Ticket ticket = AsyncCompute::k_no_ticket;
		/// Frames in flight still binding this slot
		uint32_t pins = 0;
		/// Step it holds and 0 while being written
		uint64_t step = 0;
		std::shared_ptr<const void> keep_alive;
	};

	void createDescriptors();
	void writeDescriptors(uint32_t slot);
	auto claimFreeSlot() -> uint32_t;
	void publish(uint32_t slot, AsyncCompute::Ticket ticket);
	/// Caller holds m_mutex
	auto newestFinishedLocked() const -> int32_t;

	Config m_config;
	const VulkanCore* m_core = nullptr;
	bool m_ready = false;

	ShaderLayout m_layout;
	std::vector<std::unique_ptr<VulkanPipeline>> m_pipelines;
	vk::raii::DescriptorPool m_pool = nullptr;

	std::vector<Buffer> m_buffers;
	std::vector<Binding> m_bindings;
	std::vector<ExternalBinding> m_externals;
	vma::raii::Buffer m_placeholder = nullptr;

	mutable std::mutex m_mutex;
	std::array<Slot, k_slot_count> m_slots;
	uint64_t m_step_counter = 0;
	int32_t m_last_written = -1;

	/// Render thread only
	std::array<int32_t, VulkanRenderer::k_frames_in_flight> m_frame_slot {};
	std::array<uint64_t, k_slot_count> m_acquired_step {};
};

}
