#include "color_scheme.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace assets {

void ColorScheme::reload(const toml::table& table) {
	Data::reload(table);
	m_lookup.clear();
	m_built = false;
}

void ColorScheme::buildLookup() const {
	if (m_built) {
		return;
	}
	m_built = true;

	if (!m_root.contains("colors")) {
		return;
	}

	const auto& arr = m_root["colors"];
	for (size_t i = 0; i < arr.size(); i++) {
		const auto& entry = arr[i];
		if (!entry.contains("name") || !entry.contains("color")) {
			continue;
		}

		auto name = entry["name"].value<std::string>();
		auto color = entry["color"].value<glm::vec4>();
		if (name && color) {
			m_lookup.insert_or_assign(std::move(*name), *color);
		}
	}
}

auto ColorScheme::color(std::string_view name) const -> std::optional<glm::vec4> {
	buildLookup();
	auto it = m_lookup.find(std::string(name));
	if (it == m_lookup.end()) {
		return std::nullopt;
	}
	return it->second;
}

auto ColorScheme::hex(std::string_view name) const -> std::optional<std::string> {
	auto c = color(name);
	if (!c) {
		return std::nullopt;
	}
	return toHex(*c);
}

auto ColorScheme::toHex(glm::vec4 color) -> std::string {
	auto channel = [](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
	return std::format("#{:02x}{:02x}{:02x}{:02x}", channel(color.r), channel(color.g), channel(color.b), channel(color.a));
}

}
