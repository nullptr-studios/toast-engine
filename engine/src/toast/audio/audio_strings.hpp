/**
 * @file audio_strings.hpp
 * @author Xein
 * @date 30 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include <toast/export.hpp>
#include <toast/assets/core_types.hpp>

namespace assets {

class TOAST_API AudioStrings : public Asset {
public:
	explicit AudioStrings(std::vector<uint8_t> data) : m_data(std::move(data)) { }

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "audio_strings";
	}

	[[nodiscard]]
	auto get() const noexcept -> const std::vector<uint8_t>&
	{
		return m_data;
	}

private:
	std::vector<uint8_t> m_data;
};

}
