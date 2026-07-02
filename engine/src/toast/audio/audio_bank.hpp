/**
 * @file audio_bank.hpp
 * @author Xein
 * @date 29 Jun 2026
 *
 * @brief Raw binary .bank blob passed directly to FMOD
 */

#pragma once
#include <toast/assets/core_types.hpp>
#include <toast/export.hpp>

namespace assets {

class TOAST_API AudioBank : public Asset {
public:
	explicit AudioBank(std::vector<uint8_t> data) : m_data(std::move(data)) { }

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "audio_bank";
	}

	[[nodiscard]]
	auto get() const noexcept -> const std::vector<uint8_t>& {
		return m_data;
	}

private:
	std::vector<uint8_t> m_data;
};

}
