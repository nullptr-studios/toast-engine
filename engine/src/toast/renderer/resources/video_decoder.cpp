#include "video_decoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace renderer {

namespace {

constexpr int k_io_buffer_size = 32 * 1024;

auto errorText(int code) -> std::string {
	std::array<char, AV_ERROR_MAX_STRING_SIZE> text {};
	av_strerror(code, text.data(), sizeof(text));
	return text.data();
}

}

auto VideoDecoder::readCallback(void* opaque, uint8_t* buffer, int size) -> int {
	auto* self = static_cast<VideoDecoder*>(opaque);
	const auto total = static_cast<int64_t>(self->m_bytes->size());
	const int64_t left = total - self->m_read_pos;
	if (left <= 0) {
		return AVERROR_EOF;
	}
	const auto count = static_cast<int>(std::min<int64_t>(left, size));
	std::memcpy(buffer, self->m_bytes->data() + self->m_read_pos, static_cast<size_t>(count));
	self->m_read_pos += count;
	return count;
}

auto VideoDecoder::seekCallback(void* opaque, int64_t offset, int whence) -> int64_t {
	auto* self = static_cast<VideoDecoder*>(opaque);
	const auto total = static_cast<int64_t>(self->m_bytes->size());
	whence &= ~AVSEEK_FORCE;
	int64_t target = 0;
	switch (whence) {
		case AVSEEK_SIZE: return total;
		case SEEK_SET: target = offset; break;
		case SEEK_CUR: target = self->m_read_pos + offset; break;
		case SEEK_END: target = total + offset; break;
		default: return -1;
	}
	if (target < 0 || target > total) {
		return -1;
	}
	self->m_read_pos = target;
	return target;
}

auto VideoDecoder::open(std::shared_ptr<const std::vector<uint8_t>> bytes, std::string_view debug_name)
    -> std::unique_ptr<VideoDecoder> {
	ZoneScoped;
	std::unique_ptr<VideoDecoder> self(new VideoDecoder());
	self->m_bytes = std::move(bytes);
	self->m_name = debug_name;
	if (self->m_bytes == nullptr || self->m_bytes->empty()) {
		TOAST_ERROR("Video", "Video '{}' is empty", self->m_name);
		return nullptr;
	}

	auto* io_buffer = static_cast<unsigned char*>(av_malloc(k_io_buffer_size));
	self->m_io = avio_alloc_context(io_buffer, k_io_buffer_size, 0, self.get(), &readCallback, nullptr, &seekCallback);
	self->m_format = avformat_alloc_context();
	if (self->m_io == nullptr || self->m_format == nullptr) {
		TOAST_ERROR("Video", "Video '{}' could not allocate FFmpeg contexts", self->m_name);
		return nullptr;
	}
	self->m_format->pb = self->m_io;
	self->m_format->flags |= AVFMT_FLAG_CUSTOM_IO;

	if (const int r = avformat_open_input(&self->m_format, nullptr, nullptr, nullptr); r < 0) {
		// avformat_open_input frees the context on failure
		self->m_format = nullptr;
		TOAST_ERROR("Video", "Video '{}' is not a readable container: {}", self->m_name, errorText(r));
		return nullptr;
	}
	if (const int r = avformat_find_stream_info(self->m_format, nullptr); r < 0) {
		TOAST_ERROR("Video", "Video '{}' has no stream info: {}", self->m_name, errorText(r));
		return nullptr;
	}

	const AVCodec* codec = nullptr;
	self->m_stream = av_find_best_stream(self->m_format, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
	if (self->m_stream < 0 || codec == nullptr) {
		TOAST_ERROR("Video", "Video '{}' has no video stream this build can decode (use VP9 .webm)", self->m_name);
		return nullptr;
	}
	AVStream* stream = self->m_format->streams[self->m_stream];

	self->m_codec = avcodec_alloc_context3(codec);
	if (self->m_codec == nullptr || avcodec_parameters_to_context(self->m_codec, stream->codecpar) < 0) {
		TOAST_ERROR("Video", "Video '{}' could not set up the {} decoder", self->m_name, codec->name);
		return nullptr;
	}
	// All decode work stays on the thread pool job that calls decodeNext
	self->m_codec->thread_count = 1;
	if (const int r = avcodec_open2(self->m_codec, codec, nullptr); r < 0) {
		TOAST_ERROR("Video", "Video '{}' could not open the {} decoder: {}", self->m_name, codec->name, errorText(r));
		return nullptr;
	}

	self->m_width = static_cast<uint32_t>(self->m_codec->width);
	self->m_height = static_cast<uint32_t>(self->m_codec->height);
	const AVRational rate = av_guess_frame_rate(self->m_format, stream, nullptr);
	self->m_fps = rate.num > 0 && rate.den > 0 ? av_q2d(rate) : 30.0;

	if (stream->nb_frames > 0) {
		self->m_frame_count = static_cast<uint32_t>(stream->nb_frames);
	} else {
		double seconds = 0.0;
		if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0) {
			seconds = static_cast<double>(stream->duration) * av_q2d(stream->time_base);
		} else if (self->m_format->duration != AV_NOPTS_VALUE && self->m_format->duration > 0) {
			seconds = static_cast<double>(self->m_format->duration) / AV_TIME_BASE;
		}
		self->m_frame_count = static_cast<uint32_t>(std::max<long long>(std::llround(seconds * self->m_fps), 0));
	}

	if (self->m_width == 0 || self->m_height == 0 || self->m_frame_count == 0) {
		TOAST_ERROR(
		    "Video", "Video '{}' reports {}x{} with {} frames", self->m_name, self->m_width, self->m_height, self->m_frame_count
		);
		return nullptr;
	}

	self->m_frame = av_frame_alloc();
	self->m_packet = av_packet_alloc();
	if (self->m_frame == nullptr || self->m_packet == nullptr) {
		TOAST_ERROR("Video", "Video '{}' could not allocate frame buffers", self->m_name);
		return nullptr;
	}
	return self;
}

VideoDecoder::~VideoDecoder() {
	sws_freeContext(m_sws);
	av_packet_free(&m_packet);
	av_frame_free(&m_frame);
	avcodec_free_context(&m_codec);
	avformat_close_input(&m_format);
	if (m_io != nullptr) {
		av_freep(&m_io->buffer);
		avio_context_free(&m_io);
	}
}

auto VideoDecoder::rewind() -> bool {
	if (av_seek_frame(m_format, m_stream, 0, AVSEEK_FLAG_BACKWARD) < 0) {
		// Some demuxers refuse a stream seek but take a byte seek to the start
		if (avformat_seek_file(m_format, -1, INT64_MIN, 0, 0, AVSEEK_FLAG_BYTE) < 0) {
			return false;
		}
	}
	avcodec_flush_buffers(m_codec);
	m_next_index = 0;
	return true;
}

auto VideoDecoder::receiveFrame() -> bool {
	bool rewound = false;
	while (true) {
		const int received = avcodec_receive_frame(m_codec, m_frame);
		if (received == 0) {
			return true;
		}
		if (received == AVERROR_EOF) {
			// Twice in a row without a frame means the file decodes to nothing
			if (rewound || !rewind()) {
				TOAST_ERROR("Video", "Video '{}' could not loop back to its first frame", m_name);
				return false;
			}
			rewound = true;
			continue;
		}
		if (received != AVERROR(EAGAIN)) {
			TOAST_ERROR("Video", "Video '{}' decode failed: {}", m_name, errorText(received));
			return false;
		}

		const int read = av_read_frame(m_format, m_packet);
		if (read == AVERROR_EOF) {
			avcodec_send_packet(m_codec, nullptr);
			continue;
		}
		if (read < 0) {
			TOAST_ERROR("Video", "Video '{}' read failed: {}", m_name, errorText(read));
			return false;
		}
		if (m_packet->stream_index == m_stream) {
			const int sent = avcodec_send_packet(m_codec, m_packet);
			if (sent < 0 && sent != AVERROR(EAGAIN) && sent != AVERROR_INVALIDDATA) {
				av_packet_unref(m_packet);
				TOAST_ERROR("Video", "Video '{}' rejected a packet: {}", m_name, errorText(sent));
				return false;
			}
		}
		av_packet_unref(m_packet);
	}
}

auto VideoDecoder::skipNext() -> std::optional<uint32_t> {
	if (!receiveFrame()) {
		return std::nullopt;
	}
	av_frame_unref(m_frame);
	return m_next_index++;
}

auto VideoDecoder::decodeNext(std::byte* dst, size_t dst_row_pitch) -> std::optional<uint32_t> {
	ZoneScoped;
	if (!receiveFrame()) {
		return std::nullopt;
	}

	const auto width = static_cast<int>(m_width);
	const auto height = static_cast<int>(m_height);
	// VP9 may switch resolution mid stream, a context built for the old size would read past the new planes
	if (m_sws == nullptr || m_sws_source_format != m_frame->format || m_sws_source_width != m_frame->width ||
	    m_sws_source_height != m_frame->height) {
		sws_freeContext(m_sws);
		m_sws = sws_getContext(
		    m_frame->width,
		    m_frame->height,
		    static_cast<AVPixelFormat>(m_frame->format),
		    width,
		    height,
		    AV_PIX_FMT_RGBA,
		    SWS_BILINEAR,
		    nullptr,
		    nullptr,
		    nullptr
		);
		if (m_sws == nullptr) {
			av_frame_unref(m_frame);
			TOAST_ERROR("Video", "Video '{}' has a pixel format swscale cannot convert", m_name);
			return std::nullopt;
		}
		m_sws_source_format = m_frame->format;
		m_sws_source_width = m_frame->width;
		m_sws_source_height = m_frame->height;

		const bool standard_definition = m_frame->colorspace == AVCOL_SPC_BT470BG || m_frame->colorspace == AVCOL_SPC_SMPTE170M;
		const int* coefficients = sws_getCoefficients(standard_definition ? SWS_CS_ITU601 : SWS_CS_ITU709);
		const int source_full_range = m_frame->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
		sws_setColorspaceDetails(m_sws, coefficients, source_full_range, coefficients, 1, 0, 1 << 16, 1 << 16);
	}

	std::array<uint8_t*, 4> planes = {reinterpret_cast<uint8_t*>(dst), nullptr, nullptr, nullptr};
	std::array<const int, 4> strides = {static_cast<int>(dst_row_pitch), 0, 0, 0};
	sws_scale(m_sws, m_frame->data, m_frame->linesize, 0, m_frame->height, planes.data(), strides.data());
	av_frame_unref(m_frame);
	return m_next_index++;
}

}
