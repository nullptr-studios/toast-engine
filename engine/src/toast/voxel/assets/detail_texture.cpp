#include "detail_texture.hpp"

#include <algorithm>
#include <array>
#include <sstream>

namespace assets {

DetailTexture::DetailTexture(std::vector<DetailTextureEntry> entries) : m_entries(std::move(entries)) { }

auto DetailTexture::entries() const -> std::vector<DetailTextureEntry> {
	const std::lock_guard lock(m_mutex);
	return m_entries;
}

auto DetailTexture::layerCount() const -> uint32_t {
	const std::lock_guard lock(m_mutex);
	uint32_t highest = 1;
	for (const DetailTextureEntry& entry : m_entries) {
		highest = std::max<uint32_t>(highest, entry.id);
	}
	// Two layers at least since a single layer view would be a plain 2D view
	return highest + 1;
}

auto DetailTexture::fromToml(const toml::table& table) -> std::unique_ptr<DetailTexture> {
	ZoneScoped;
	return std::make_unique<DetailTexture>(parseToml(table));
}

void DetailTexture::reload(const toml::table& table) {
	ZoneScoped;
	std::vector<DetailTextureEntry> parsed = parseToml(table);
	{
		const std::lock_guard lock(m_mutex);
		m_entries = std::move(parsed);
	}
	m_revision.fetch_add(1, std::memory_order_acq_rel);
}

auto DetailTexture::parseToml(const toml::table& table) -> std::vector<DetailTextureEntry> {
	ZoneScoped;

	std::vector<DetailTextureEntry> out;
	std::array<bool, k_max_detail_textures + 1> seen {};

	const toml::array* list = table["textures"].as_array();
	if (list == nullptr) {
		return out;
	}

	for (size_t i = 0; i < list->size(); ++i) {
		const toml::table* entry = (*list)[i].as_table();
		const auto fail = [&](const std::string& what) {
			return std::runtime_error("detail texture: entry " + std::to_string(i) + ": " + what);
		};
		if (entry == nullptr) {
			throw fail("is not a table");
		}

		const std::optional<int64_t> id = entry->get("id") != nullptr ? entry->get("id")->value<int64_t>() : std::nullopt;
		if (!id || *id < 1 || *id > static_cast<int64_t>(k_max_detail_textures)) {
			throw fail("id must be a whole number 1-255");
		}
		if (seen[static_cast<size_t>(*id)]) {
			throw fail("id " + std::to_string(*id) + " appears twice");
		}
		seen[static_cast<size_t>(*id)] = true;

		DetailTextureEntry parsed;
		parsed.id = static_cast<uint8_t>(*id);
		parsed.name = entry->get("name") != nullptr ? entry->get("name")->value_or(std::string {}) : std::string {};

		const auto source_uid = [&](std::string_view key) -> uint64_t {
			const toml::node* node = entry->get(key);
			if (node == nullptr) {
				return 0;
			}
			const std::optional<std::string> text = node->value<std::string>();
			const uint64_t uid = text ? toast::UID::fromString(*text) : 0;
			if (uid == 0 && text && !text->empty()) {
				throw fail(std::string(key) + " is not a valid asset UID");
			}
			return uid;
		};
		parsed.albedo_uid = source_uid("albedo");
		parsed.roughness_uid = source_uid("roughness");
		parsed.height_uid = source_uid("height");
		out.push_back(std::move(parsed));
	}
	return out;
}

auto DetailTexture::serialize(SaveMode /*mode*/) const -> std::vector<uint8_t> {
	ZoneScoped;

	toml::array list;
	for (const DetailTextureEntry& entry : m_entries) {
		toml::table out;
		out.insert("id", static_cast<int64_t>(entry.id));
		out.insert("name", entry.name);
		if (entry.albedo_uid != 0) {
			out.insert("albedo", toast::UID::toString(entry.albedo_uid));
		}
		if (entry.roughness_uid != 0) {
			out.insert("roughness", toast::UID::toString(entry.roughness_uid));
		}
		if (entry.height_uid != 0) {
			out.insert("height", toast::UID::toString(entry.height_uid));
		}
		list.push_back(std::move(out));
	}

	toml::table root;
	root.insert("textures", std::move(list));

	std::ostringstream stream;
	stream << root;
	const std::string text = stream.str();
	return {text.begin(), text.end()};
}

}
