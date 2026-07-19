/**
 * @file color_scheme.hpp
 * @author Xein
 * @date 17 Jul 2026
 *
 * @brief Maps color names to RGBA values for UI styling and text formatting
 */

#pragma once
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <toast/assets/data.hpp>
#include <toast/export.hpp>
#include <unordered_map>

namespace assets {

class TOAST_API ColorScheme : public Data {
public:
	explicit ColorScheme(const toml::table& table, Handle<Schema> schema = {}) : Data(table, std::move(schema)) { }

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "color_scheme";
	}

	/// Looks a color up by name
	[[nodiscard]]
	auto color(std::string_view name) const -> std::optional<glm::vec4>;

	/// Same lookup returning a #RRGGBBAA string
	[[nodiscard]]
	auto hex(std::string_view name) const -> std::optional<std::string>;

	/// Formats a color as #RRGGBBAA
	[[nodiscard]]
	static auto toHex(glm::vec4 color) -> std::string;

private:
	void buildLookup() const;

	mutable bool m_built = false;
	mutable std::unordered_map<std::string, glm::vec4> m_lookup;
};

}
