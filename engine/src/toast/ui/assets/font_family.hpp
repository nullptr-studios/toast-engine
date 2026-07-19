/**
 * @file font_family.hpp
 * @author Xein
 * @date 17 Jul 2026
 *
 * @brief Group of fonts loaded together
 */

#pragma once
#include <toast/assets/data.hpp>
#include <toast/export.hpp>
#include <toast/uid.hpp>
#include <vector>

namespace assets {

class TOAST_API FontFamily : public Data {
public:
	explicit FontFamily(const toml::table& table, Handle<Schema> schema = {}) : Data(table, std::move(schema)) { }

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "font_family";
	}

	[[nodiscard]]
	auto name() const -> std::string_view {
		return m_root.contains("name") ? m_root["name"].as<std::string_view>() : "";
	}

	[[nodiscard]]
	auto fonts() const -> std::vector<toast::UID> {
		std::vector<toast::UID> result;
		if (!m_root.contains("fonts")) {
			return result;
		}

		const auto& arr = m_root["fonts"];
		for (size_t i = 0; i < arr.size(); i++) {
			if (auto uid = arr[i].value<toast::UID>()) {
				result.push_back(*uid);
			}
		}
		return result;
	}
};

}
