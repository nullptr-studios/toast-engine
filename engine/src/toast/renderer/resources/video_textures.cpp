#include "video_textures.hpp"

#include "toast/renderer/resources/video_decoder.hpp"
#include "toast/renderer/resources/vulkan_texture.hpp"
#include "toast/renderer/settings/renderer_params.hpp"
#include "toast/renderer/vulkan_renderer.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>
#include <toast/assets/video.hpp>
#include <toast/thread_pool.hpp>
#include <unordered_map>
#include <variant>

namespace renderer {

namespace {

constexpr uint32_t k_ring = VulkanRenderer::k_frames_in_flight;
/// Most frames one job decodes to catch up, so a job stays a few frame decodes long
constexpr uint32_t k_max_frames_per_job = 4;
constexpr uint64_t k_none = std::numeric_limits<uint64_t>::max();
constexpr uint32_t k_fresh = 4;

struct DecodeState {
	std::unique_ptr<VideoDecoder> decoder;
	std::string name;
	uint32_t width = 0;
	uint32_t height = 0;
	double fps = 0.0;
	size_t row_pitch = 0;
	size_t frame_bytes = 0;
	/// Shrinks to the real count when the file loops earlier than its header promised
	std::atomic<uint32_t> frame_count {0};
	std::atomic<bool> job_in_flight {false};
	std::atomic<bool> failed {false};
	/// Set by the open job once the fields above are filled
	std::atomic<bool> opened {false};

	// Cache mode, every frame of the loop in host memory, filled once in order
	std::optional<vma::raii::Buffer> cache;
	std::byte* cache_ptr = nullptr;
	size_t cache_reserved = 0;
	std::atomic<uint32_t> filled {0};
	std::atomic<bool> cached {false};

	// Stream mode, triple buffer, the job owns back, the render thread owns front
	std::array<std::vector<std::byte>, 3> slots;
	std::array<uint64_t, 3> slot_sequence {};
	uint32_t back = 0;
	std::atomic<uint32_t> middle {1};
	uint32_t front = 2;
	std::atomic<uint32_t> published_index {std::numeric_limits<uint32_t>::max()};
	uint64_t produced = 0;
	uint32_t next_index = 0;

	[[nodiscard]]
	auto streaming() const -> bool {
		return cache_ptr == nullptr;
	}
};

struct GpuState {
	/// Keeps the cache alive while a copy from it may still run on the GPU
	std::shared_ptr<DecodeState> decode;
	std::array<VulkanTexture, k_ring> images;
	std::array<std::optional<vma::raii::Buffer>, k_ring> staging;
	std::array<uint64_t, k_ring> held {};
	uint64_t start_frame = k_none;

	GpuState() { held.fill(k_none); }
};

struct Entry {
	std::shared_ptr<const std::vector<uint8_t>> bytes;
	uint32_t id = 0;
	std::shared_ptr<DecodeState> decode;
	std::shared_ptr<GpuState> gpu;
};

struct Registry {
	std::mutex mutex;
	std::unordered_map<const assets::Video*, Entry> videos;
	std::vector<std::shared_ptr<void>> retired;
	size_t cache_used = 0;
	uint32_t next_id = 0;
	std::optional<std::chrono::steady_clock::time_point> epoch;
	std::atomic<uint32_t> jobs_in_flight {0};
};

auto registry() -> Registry& {
	static Registry r;
	return r;
}

auto cacheBudgetBytes() -> size_t {
	const int64_t megabytes = rendererParams().video_frame_cache_mb;
	return megabytes > 0 ? static_cast<size_t>(megabytes) << 20 : 0;
}

auto createHostBuffer(const VulkanCore& core, size_t bytes, vma::AllocationCreateFlags access, const std::string& name)
    -> vma::raii::Buffer {
	vk::BufferCreateInfo buffer_ci {};
	buffer_ci.size = (bytes);
	buffer_ci.usage = vk::BufferUsageFlagBits::eTransferSrc;
	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAutoPreferHost;
	alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | access;
	auto buffer = core.getAllocator().createBuffer(buffer_ci, alloc_ci);
	setDebugName(core, *buffer, name);
	return buffer;
}

/// Opening reads stream info
void startOpen(Entry& entry, Registry& r) {
	auto state = std::make_shared<DecodeState>();
	state->name = std::format("Video {}", entry.id);
	entry.decode = state;
	state->job_in_flight.store(true, std::memory_order_release);
	r.jobs_in_flight.fetch_add(1, std::memory_order_acq_rel);
	toast::ThreadPool::lpPushRaw([state, bytes = entry.bytes, &r]() mutable {
		ZoneScopedN("Video open");
		DecodeState& d = *state;
		d.decoder = VideoDecoder::open(bytes, d.name);
		if (d.decoder != nullptr) {
			d.width = d.decoder->width();
			d.height = d.decoder->height();
			d.fps = d.decoder->fps();
			d.row_pitch = static_cast<size_t>(d.width) * 4;
			d.frame_bytes = d.row_pitch * d.height;
			d.frame_count = d.decoder->frameCount();
		} else {
			d.failed = true;
		}
		d.opened.store(true, std::memory_order_release);
		d.job_in_flight.store(false, std::memory_order_release);
		// Last reference may own VMA memory
		state.reset();
		bytes.reset();
		r.jobs_in_flight.fetch_sub(1, std::memory_order_acq_rel);
	});
}

void createResources(Entry& entry, const VulkanCore& core, Registry& r) {
	ZoneScoped;
	DecodeState& d = *entry.decode;
	try {
		const size_t budget = cacheBudgetBytes();
		const size_t need = d.frame_bytes * d.frame_count;
		if (need <= budget - std::min(budget, r.cache_used)) {
			d.cache = createHostBuffer(core, need, vma::AllocationCreateFlagBits::eHostAccessRandom, d.name + " FrameCache");
			d.cache_ptr = static_cast<std::byte*>(d.cache->getAllocation().getInfo().pMappedData);
			if (d.cache_ptr != nullptr) {
				d.cache_reserved = need;
				r.cache_used += need;
			} else {
				d.cache.reset();
			}
		}
	} catch (const std::exception& e) {
		TOAST_WARN("Video", "{} frame cache allocation failed, decoding every frame instead: {}", d.name, e.what());
		d.cache.reset();
		d.cache_ptr = nullptr;
	}

	auto gpu = std::make_shared<GpuState>();
	gpu->decode = entry.decode;
	try {
		for (uint32_t i = 0; i < k_ring; ++i) {
			gpu->images[i].create(
			    core,
			    VulkanTexture::Params {.format = vk::Format::eR8G8B8A8Srgb, .extent = vk::Extent3D(d.width, d.height, 1)},
			    std::format("{} Image{}", d.name, i)
			);
			gpu->images[i].markReady();
			if (d.streaming()) {
				gpu->staging[i] = createHostBuffer(
				    core, d.frame_bytes, vma::AllocationCreateFlagBits::eHostAccessSequentialWrite, std::format("{} Staging{}", d.name, i)
				);
			}
		}
		if (d.streaming()) {
			for (auto& slot : d.slots) {
				slot.resize(d.frame_bytes);
			}
		}
	} catch (const std::exception& e) {
		TOAST_ERROR("Video", "{} could not create its {}x{} images: {}", d.name, d.width, d.height, e.what());
		d.failed = true;
		return;
	}
	entry.gpu = std::move(gpu);
}

void fillCache(DecodeState& d, uint64_t position) {
	uint32_t filled = d.filled.load(std::memory_order_relaxed);
	for (uint32_t step = 0; step < k_max_frames_per_job && filled < d.frame_count && filled <= position; ++step) {
		const size_t offset = d.frame_bytes * filled;
		const auto index = d.decoder->decodeNext(d.cache_ptr + offset, d.row_pitch);
		if (!index) {
			d.failed = true;
			return;
		}
		if (*index != filled) {
			// Looped before the header frame count, so the loop is shorter than estimated
			d.frame_count = filled;
			break;
		}
		d.cache->getAllocation().flush(offset, d.frame_bytes);
		d.filled.store(++filled, std::memory_order_release);
	}
	if (filled > 0 && filled >= d.frame_count) {
		d.decoder.reset();
		d.cached.store(true, std::memory_order_release);
	}
}

void streamFrame(DecodeState& d, uint64_t position) {
	const uint32_t count = d.frame_count;
	const auto target = static_cast<uint32_t>(position % count);
	const uint32_t behind = (target + count - d.next_index) % count;
	// wait for it
	if (behind > count / 2) {
		return;
	}

	const auto advance = [&d](std::optional<uint32_t> index) -> bool {
		if (!index) {
			d.failed = true;
			return false;
		}
		if (*index == 0 && d.next_index != 0) {
			d.frame_count = d.next_index;
		} else if (*index >= d.frame_count) {
			// The header undercounted
			d.frame_count = *index + 1;
		}
		d.next_index = (*index + 1) % d.frame_count;
		return true;
	};

	for (uint32_t skipped = 0; skipped < std::min(behind, k_max_frames_per_job - 1); ++skipped) {
		if (!advance(d.decoder->skipNext())) {
			return;
		}
	}
	const auto index = d.decoder->decodeNext(d.slots[d.back].data(), d.row_pitch);
	if (!advance(index)) {
		return;
	}
	d.slot_sequence[d.back] = ++d.produced;
	d.back = d.middle.exchange(d.back | k_fresh, std::memory_order_acq_rel) & 3;
	d.published_index.store(*index, std::memory_order_release);
}

void schedule(const std::shared_ptr<DecodeState>& state, uint64_t position, Registry& r, uint32_t cap) {
	DecodeState& d = *state;
	if (d.failed || d.cached.load(std::memory_order_acquire) || d.job_in_flight.load(std::memory_order_acquire)) {
		return;
	}
	if (!d.streaming()) {
		if (d.filled.load(std::memory_order_acquire) > position) {
			return;
		}
	} else if (d.published_index.load(std::memory_order_acquire) == position % d.frame_count) {
		return;
	}
	if (r.jobs_in_flight.load(std::memory_order_acquire) >= cap) {
		return;
	}

	d.job_in_flight.store(true, std::memory_order_release);
	r.jobs_in_flight.fetch_add(1, std::memory_order_acq_rel);
	toast::ThreadPool::lpPushRaw([state = state, position, &r]() mutable {
		ZoneScopedN("Video decode");
		if (state->streaming()) {
			streamFrame(*state, position);
		} else {
			fillCache(*state, position);
		}
		state->job_in_flight.store(false, std::memory_order_release);
		state.reset();
		r.jobs_in_flight.fetch_sub(1, std::memory_order_acq_rel);
	});
}

void copyFrame(vk::CommandBuffer cmd, uint32_t frame_index, GpuState& g, DecodeState& d, uint64_t position) {
	vk::Buffer source;
	vk::DeviceSize offset = 0;
	uint64_t id = 0;
	if (!d.streaming()) {
		const uint32_t filled = d.filled.load(std::memory_order_acquire);
		if (filled == 0) {
			return;
		}
		const bool cached = d.cached.load(std::memory_order_acquire);
		const uint64_t frame = cached ? position % d.frame_count : std::min<uint64_t>(position, filled - 1);
		id = frame;
		if (g.held[frame_index] == id) {
			return;
		}
		source = **d.cache;
		offset = static_cast<vk::DeviceSize>(frame * d.frame_bytes);
	} else {
		if ((d.middle.load(std::memory_order_acquire) & k_fresh) != 0) {
			d.front = d.middle.exchange(d.front, std::memory_order_acq_rel) & 3;
		}
		id = d.slot_sequence[d.front];
		if (id == 0 || g.held[frame_index] == id) {
			return;
		}
		auto& staging = *g.staging[frame_index];
		std::memcpy(staging.getAllocation().getInfo().pMappedData, d.slots[d.front].data(), d.frame_bytes);
		staging.getAllocation().flush(0, d.frame_bytes);
		source = *staging;
	}

	const vk::Image image = g.images[frame_index].getImage();
	const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
	recordUndefinedToTransferDst(cmd, image, range);
	vk::BufferImageCopy copy {};
	copy.bufferOffset = offset;
	copy.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
	copy.imageExtent = vk::Extent3D(d.width, d.height, 1);
	cmd.copyBufferToImage(source, image, vk::ImageLayout::eTransferDstOptimal, copy);
	recordTransferDstToShaderRead(cmd, image, range);
	g.held[frame_index] = id;
}

}

void VideoTextures::registerVideo(const assets::Video& video) {
	auto& r = registry();
	std::lock_guard lock(r.mutex);
	r.videos[&video] = Entry {.bytes = video.bytes(), .id = r.next_id++};
}

void VideoTextures::unregisterVideo(const assets::Video& video) {
	auto& r = registry();
	std::lock_guard lock(r.mutex);
	const auto it = r.videos.find(&video);
	if (it == r.videos.end()) {
		return;
	}
	if (it->second.decode != nullptr) {
		r.cache_used -= std::min(r.cache_used, it->second.decode->cache_reserved);
	}
	// Jobs hold the decode state themselves, gpu waits
	if (it->second.gpu != nullptr) {
		r.retired.push_back(std::move(it->second.gpu));
	}
	r.videos.erase(it);
}

auto VideoTextures::view(const assets::Video& video, uint32_t frame_index) -> std::optional<std::pair<vk::ImageView, uint64_t>> {
	auto& r = registry();
	std::lock_guard lock(r.mutex);
	const auto it = r.videos.find(&video);
	if (it == r.videos.end() || it->second.gpu == nullptr || frame_index >= k_ring) {
		return std::nullopt;
	}
	const GpuState& g = *it->second.gpu;
	if (g.held[frame_index] == k_none) {
		return std::nullopt;
	}
	return std::pair {g.images[frame_index].getView(), g.images[frame_index].generation()};
}

auto VideoTextures::failed(const assets::Video& video) -> bool {
	auto& r = registry();
	std::lock_guard lock(r.mutex);
	const auto it = r.videos.find(&video);
	return it != r.videos.end() && it->second.decode != nullptr && it->second.decode->failed;
}

void VideoTextures::record(vk::CommandBuffer cmd, uint32_t frame_index) {
	ZoneScoped;
	auto* renderer = VulkanRenderer::instance;
	auto& r = registry();
	std::lock_guard lock(r.mutex);
	if (renderer == nullptr) {
		return;
	}
	for (auto& object : r.retired) {
		renderer->retire(std::move(object));
	}
	r.retired.clear();
	if (r.videos.empty()) {
		return;
	}

	const auto now = std::chrono::steady_clock::now();
	if (!r.epoch) {
		r.epoch = now;
	}
	const double seconds = std::chrono::duration<double>(now - *r.epoch).count();
	const auto cap = static_cast<uint32_t>(std::max<size_t>(toast::ThreadPool::workerCount() / 4, 1));
	const VulkanCore& core = renderer->getCore();

	for (auto& [video, entry] : r.videos) {
		if (entry.decode == nullptr) {
			startOpen(entry, r);
		}
		if (entry.gpu == nullptr) {
			if (!entry.decode->opened.load(std::memory_order_acquire) || entry.decode->failed) {
				continue;
			}
			createResources(entry, core, r);
			if (entry.gpu == nullptr) {
				continue;
			}
		}
		DecodeState& d = *entry.decode;
		GpuState& g = *entry.gpu;
		const auto clock_frame = static_cast<uint64_t>(std::floor(seconds * d.fps));
		if (g.start_frame == k_none) {
			g.start_frame = clock_frame;
		}
		const uint64_t position = clock_frame - g.start_frame;
		schedule(entry.decode, position, r, cap);
		copyFrame(cmd, frame_index, g, d, position);
	}
}

void VideoTextures::shutdown() {
	auto& r = registry();
	while (r.jobs_in_flight.load(std::memory_order_acquire) > 0) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	std::lock_guard lock(r.mutex);
	r.retired.clear();
	for (auto& [video, entry] : r.videos) {
		entry.gpu.reset();
		entry.decode.reset();
	}
	r.cache_used = 0;
}

}
