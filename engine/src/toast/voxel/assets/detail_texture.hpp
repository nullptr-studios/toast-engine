/**
 * @file detail_texture.hpp
 * @author Xein
 * @date 14 Nov 2026
 */

#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <toast/assets/core_types.hpp>
#include <vector>

namespace assets {

inline constexpr uint32_t k_max_detail_textures = 255;

struct DetailTextureEntry {
	uint8_t id = 0;

	std::string name;

	uint64_t albedo_uid = 0;
	uint64_t roughness_uid = 0;
	uint64_t height_uid = 0;

	[[nodiscard]]
	auto operator==(const DetailTextureEntry&) const -> bool = default;
};

class TOAST_API DetailTexture : public Asset, public ISaveable {
public:
	explicit DetailTexture(std::vector<DetailTextureEntry> entries);

	[[nodiscard]]
	static auto fromToml(const toml::table& table) -> std::unique_ptr<DetailTexture>;

	void reload(const toml::table& table);

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "detail_texture";
	}

	[[nodiscard]]
	auto serialize(SaveMode mode) const -> std::vector<uint8_t> override;

	[[nodiscard]]
	auto entries() const -> std::vector<DetailTextureEntry>;

	[[nodiscard]]
	auto layerCount() const -> uint32_t;

	[[nodiscard]]
	auto revision() const noexcept -> uint32_t {
		return m_revision.load(std::memory_order_acquire);
	}

private:
	[[nodiscard]]
	static auto parseToml(const toml::table& table) -> std::vector<DetailTextureEntry>;

	mutable std::mutex m_mutex;
	std::vector<DetailTextureEntry> m_entries;
	std::atomic<uint32_t> m_revision {0};
};

}
