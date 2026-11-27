#include "video.hpp"

#include <toast/renderer/resources/video_textures.hpp>

namespace assets {

Video::Video(std::vector<uint8_t> data) : m_bytes(std::make_shared<const std::vector<uint8_t>>(std::move(data))) {
	renderer::VideoTextures::registerVideo(*this);
}

Video::~Video() {
	renderer::VideoTextures::unregisterVideo(*this);
}

}
