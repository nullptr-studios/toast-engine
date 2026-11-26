/// @file video_decoder.hpp
/// @author dario
/// @date 26 Nov 2026

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct AVFormatContext;
struct AVIOContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace renderer {

/// Not thread safe
class VideoDecoder {
public:
	/// @returns nullptr after logging when the bytes hold no decodable video stream
	static auto open(std::shared_ptr<const std::vector<uint8_t>> bytes, std::string_view debug_name)
	    -> std::unique_ptr<VideoDecoder>;

	~VideoDecoder();
	VideoDecoder(const VideoDecoder&) = delete;
	auto operator=(const VideoDecoder&) -> VideoDecoder& = delete;

	[[nodiscard]]
	auto width() const -> uint32_t {
		return m_width;
	}

	[[nodiscard]]
	auto height() const -> uint32_t {
		return m_height;
	}

	[[nodiscard]]
	auto fps() const -> double {
		return m_fps;
	}

	[[nodiscard]]
	auto frameCount() const -> uint32_t {
		return m_frame_count;
	}

	/// Decodes the next frame into @p dst as RGBA8 rows @p dst_row_pitch apart
	/// @returns the frame index since the start of the file
	auto decodeNext(std::byte* dst, size_t dst_row_pitch) -> std::optional<uint32_t>;

	/// to catch up
	auto skipNext() -> std::optional<uint32_t>;

private:
	VideoDecoder() = default;

	static auto readCallback(void* opaque, uint8_t* buffer, int size) -> int;
	static auto seekCallback(void* opaque, int64_t offset, int whence) -> int64_t;

	auto receiveFrame() -> bool;
	auto rewind() -> bool;

	std::shared_ptr<const std::vector<uint8_t>> m_bytes;
	std::string m_name;
	int64_t m_read_pos = 0;

	AVIOContext* m_io = nullptr;
	AVFormatContext* m_format = nullptr;
	AVCodecContext* m_codec = nullptr;
	AVFrame* m_frame = nullptr;
	AVPacket* m_packet = nullptr;
	SwsContext* m_sws = nullptr;
	int m_stream = -1;
	int m_sws_source_format = -1;
	int m_sws_source_width = 0;
	int m_sws_source_height = 0;

	uint32_t m_width = 0;
	uint32_t m_height = 0;
	double m_fps = 0.0;
	uint32_t m_frame_count = 0;
	uint32_t m_next_index = 0;
};

}
