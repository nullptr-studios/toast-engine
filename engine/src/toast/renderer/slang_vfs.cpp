#include "slang_vfs.hpp"

#include <atomic>
#include <cstring>
#include <string>
#include <toast/assets/asset_manager.hpp>
#include <toast/log.hpp>
#include <vector>

namespace renderer {

namespace {

auto sameGuid(const SlangUUID& a, const SlangUUID& b) -> bool {
	return std::memcmp(&a, &b, sizeof(SlangUUID)) == 0;
}

class VfsBlob final : public ISlangBlob {
public:
	explicit VfsBlob(std::vector<uint8_t> data) : m_data(std::move(data)) { }

	SLANG_NO_THROW auto SLANG_MCALL queryInterface(const SlangUUID& uuid, void** out_object) -> SlangResult override {
		if (out_object == nullptr) {
			return SLANG_E_INVALID_ARG;
		}
		if (sameGuid(uuid, ISlangUnknown::getTypeGuid()) || sameGuid(uuid, ISlangBlob::getTypeGuid())) {
			addRef();
			*out_object = this;
			return SLANG_OK;
		}
		*out_object = nullptr;
		return SLANG_E_NO_INTERFACE;
	}

	SLANG_NO_THROW auto SLANG_MCALL addRef() -> uint32_t override { return ++m_refs; }

	SLANG_NO_THROW auto SLANG_MCALL release() -> uint32_t override {
		const uint32_t refs = --m_refs;
		if (refs == 0) {
			delete this;
		}
		return refs;
	}

	SLANG_NO_THROW auto getBufferPointer() -> const void* SLANG_MCALL override { return m_data.data(); }

	SLANG_NO_THROW auto SLANG_MCALL getBufferSize() -> size_t override { return m_data.size(); }

private:
	std::vector<uint8_t> m_data;
	std::atomic<uint32_t> m_refs {1};
};

}

auto SlangVfs::get() -> SlangVfs& {
	static SlangVfs instance;
	return instance;
}

auto SlangVfs::queryInterface(const SlangUUID& uuid, void** out_object) -> SlangResult {
	if (out_object == nullptr) {
		return SLANG_E_INVALID_ARG;
	}
	if (sameGuid(uuid, ISlangUnknown::getTypeGuid()) || sameGuid(uuid, ISlangCastable::getTypeGuid()) ||
	    sameGuid(uuid, ISlangFileSystem::getTypeGuid())) {
		*out_object = this;
		return SLANG_OK;
	}
	*out_object = nullptr;
	return SLANG_E_NO_INTERFACE;
}

auto SlangVfs::castAs(const SlangUUID& uuid) -> void* {
	void* object = nullptr;
	if (queryInterface(uuid, &object) == SLANG_OK) {
		return object;
	}
	return nullptr;
}

auto SlangVfs::normalizeUri(std::string_view path) -> std::string {
	std::string_view in = path;

	// Strip any ./ or .\ prefixes
	while (in.starts_with("./") || in.starts_with(".\\")) {
		in.remove_prefix(2);
	}

	// Locate the scheme separator
	auto sep = in.find("://");
	size_t rel_start = 0;
	if (sep != std::string_view::npos) {
		rel_start = sep + 3;
	} else {
		sep = in.find(":/");
		if (sep == std::string_view::npos) {
			return {};
		}
		rel_start = sep + 2;
	}

	const std::string_view scheme = in.substr(0, sep);
	std::string_view rel = in.substr(rel_start);
	while (rel.starts_with('/')) {
		rel.remove_prefix(1);
	}

	// Collapse duplicate slashes and backslashes in the relative part
	std::string clean_rel;
	clean_rel.reserve(rel.size());
	char prev = '\0';
	for (char c : rel) {
		if (c == '\\') {
			c = '/';
		}
		if (c == '/' && prev == '/') {
			continue;
		}
		clean_rel.push_back(c);
		prev = c;
	}

	return std::string(scheme) + "://" + clean_rel;
}

auto SlangVfs::makeBlob(const void* data, size_t size) -> ISlangBlob* {
	const auto* bytes = static_cast<const uint8_t*>(data);
	return new VfsBlob(std::vector<uint8_t>(bytes, bytes + size));
}

auto SlangVfs::loadFile(const char* path, ISlangBlob** out_blob) -> SlangResult {
	if (path == nullptr || out_blob == nullptr) {
		return SLANG_E_INVALID_ARG;
	}
	*out_blob = nullptr;

	const std::string uri = normalizeUri(path);
	if (uri.empty()) {
		// Not a virtual URI
		return SLANG_E_NOT_FOUND;
	}

	auto bytes = assets::AssetManager::get().tryLoadBytes(uri);
	if (!bytes) {
		return SLANG_E_NOT_FOUND;
	}

	TOAST_TRACE("Render", "SlangVfs resolved '{}' ({} bytes)", uri, bytes->size());
	*out_blob = new VfsBlob(std::move(*bytes));
	return SLANG_OK;
}

}
