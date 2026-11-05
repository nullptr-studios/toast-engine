#include "pack.hpp"

namespace assets {

PackArchive::PackArchive(const std::filesystem::path& path) : m_reader(path) {
	ZoneScoped;
	TOAST_INFO("PackArchive", "Mounted {} ({} entries)", path.string(), m_reader.entryCount());
}

auto PackArchive::read(std::string_view rel_path) const -> std::optional<std::vector<uint8_t>> {
	ZoneScoped;
	// Normalise to forward slashes before hashing
	std::string key(rel_path);
	for (auto& c : key) {
		if (c == '\\') {
			c = '/';
		}
	}

	const pack::Entry* e = m_reader.findByHash(pack::fnv1a64(key));
	if (e == nullptr) {
		return std::nullopt;
	}

	// Verify path
	if (e->rel != key) {
		TOAST_WARN("PackArchive", "Hash collision for '{}' vs '{}' in {}", key, e->rel, path().string());
		return std::nullopt;
	}

	try {
		return m_reader.readEntry(*e);
	} catch (const std::exception& ex) {
		TOAST_ERROR("PackArchive", "Failed to read '{}' from {}: {}", key, path().string(), ex.what());
		return std::nullopt;
	}
}

}
