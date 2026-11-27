/**
 * @file post_process_profile.hpp
 * @author dario
 * @date 24/11/2026
 */

#pragma once
#include <atomic>
#include <toast/assets/data.hpp>

namespace assets {

class TOAST_API PostProcessProfile : public Data {
public:
	explicit PostProcessProfile(const toml::table& table, Handle<Schema> schema = {});

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "post_process";
	}

	/// Bumps whenever the file is re-read
	[[nodiscard]]
	auto version() const noexcept -> uint32_t {
		return m_version.load(std::memory_order_relaxed);
	}

protected:
	void onReloaded() override;

private:
	std::atomic<uint32_t> m_version {0};
};

}
