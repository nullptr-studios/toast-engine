/// @file video_textures.hpp
/// @author dario
/// @date 26 Nov 2026

#pragma once

#include <cstdint>
#include <optional>
#include <toast/export.hpp>
#include <utility>
#include <vulkan/vulkan.hpp>

namespace assets {
class Video;
}

namespace renderer {

/// Every loaded video decodes once on the thread pool and shows through one image per frame in flight
class VideoTextures {
public:
	TOAST_API static void registerVideo(const assets::Video& video);
	TOAST_API static void unregisterVideo(const assets::Video& video);

	/// The image @p frame_index samples and its generation
	static auto view(const assets::Video& video, uint32_t frame_index) -> std::optional<std::pair<vk::ImageView, uint64_t>>;

	/// True if video failed
	static auto failed(const assets::Video& video) -> bool;

	/// once per frame before anything samples a video
	static void record(vk::CommandBuffer cmd, uint32_t frame_index);

	static void shutdown();
};

}
