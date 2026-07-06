#include "material.hpp"

#include "assets.hpp"
#include "texture.hpp"

#include <sstream>

namespace assets {

Material::Material(toml::table table) : m_table(std::move(table)) {
	const auto* params = m_table.get_as<toml::table>("params");
	if (!params) {
		return;
	}

	const auto* albedo = params->get_as<std::string>("albedo_map");
	if (!albedo || albedo->get().empty()) {
		return;
	}

	const std::string& ref = albedo->get();
	if (ref.size() == 11) {
		m_albedo_uid = toast::UID(toast::UID::fromString(ref));
		return;
	}

	if (const auto uid = assets::resolveURI(ref)) {
		m_albedo_uid = *uid;
	}
}

auto Material::get() const noexcept -> const toml::table& {
	return m_table;
}

auto Material::get() noexcept -> toml::table& {
	return m_table;
}

auto Material::serialize(SaveMode mode) const -> std::vector<uint8_t> {
	(void)mode;

	std::ostringstream ss;
	ss << m_table;
	const std::string text = ss.str();
	return {text.begin(), text.end()};
}

void Material::resolveTextureHandles() {
	if (m_albedo_map.hasValue() || m_albedo_uid.data() == 0) {
		return;
	}

	m_albedo_map = assets::load<Texture>(m_albedo_uid);
}

}
