/**
 * @file audio_vca.hpp
 * @author Xein
 * @date 30 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include <toast/assets/data.hpp>
#include <toast/export.hpp>

namespace assets {

class TOAST_API AudioVca : public Data {
public:
	explicit AudioVca(const toml::table& table, AssetHandle<Schema> schema = {}) : Data(table, std::move(schema)) { }

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "audio_vca";
	}

	// Type-safe string getters pulling from the underlying m_root DataValue
	[[nodiscard]]
	auto eventType() const -> std::string_view {
		return m_root.contains("type") ? m_root["type"].as<std::string_view>() : "";
	}

	[[nodiscard]]
	auto name() const -> std::string_view {
		return m_root.contains("name") ? m_root["name"].as<std::string_view>() : "";
	}

	[[nodiscard]]
	auto path() const -> std::string_view {
		return m_root.contains("path") ? m_root["path"].as<std::string_view>() : "";
	}

	[[nodiscard]]
	auto guid() const -> std::string_view {
		return m_root.contains("guid") ? m_root["guid"].as<std::string_view>() : "";
	}

	void setVolume(float volume) const;
};

}
