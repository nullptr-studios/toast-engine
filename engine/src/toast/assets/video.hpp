/**
 * @file video.hpp
 * @author dario
 * @date 26 Nov 2026
 *
 * @brief Encoded video file played as a looping texture
 */

#pragma once
#include "core_types.hpp"

#include <memory>
#include <toast/export.hpp>
#include <vector>

namespace assets {

class TOAST_API Video : public Asset {
public:
	explicit Video(std::vector<uint8_t> data);
	~Video() override;

	Video(const Video&) = delete;
	auto operator=(const Video&) -> Video& = delete;

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "video";
	}

	/// Shared so a decode job still running after asset unloads keeps reading valid bytes
	[[nodiscard]]
	auto bytes() const noexcept -> const std::shared_ptr<const std::vector<uint8_t>>& {
		return m_bytes;
	}

private:
	std::shared_ptr<const std::vector<uint8_t>> m_bytes;
};

}
