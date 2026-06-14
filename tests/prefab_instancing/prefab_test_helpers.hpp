#pragma once

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <toast/assets/core_types.hpp>
#include <toast/assets/prefab.hpp>
#include <toast/uid.hpp>
#include <unordered_map>

namespace toast::tests {

// A pure in-memory prefab store: maps an asset UID to a parsed Prefab and hands instantiate a
// resolver, so prefab instancing can be exercised without an AssetManager.
struct PrefabStore {
	std::unordered_map<uint64_t, std::unique_ptr<assets::Prefab>> assets;

	void add(std::string_view uid_str, const std::string& text) {
		std::stringstream ss(text);
		assets[UID::fromString(uid_str)] = std::make_unique<assets::Prefab>(ss);
	}

	auto resolver() {
		return [this](UID id) -> assets::AssetHandle<assets::Prefab> {
			auto it = assets.find(id.data());
			return it != assets.end() ? assets::AssetHandle<assets::Prefab>(it->second.get(), id)
			                          : assets::AssetHandle<assets::Prefab>(nullptr, id);
		};
	}

	auto handle(std::string_view uid_str) -> assets::AssetHandle<assets::Prefab> {
		UID id(UID::fromString(uid_str));
		return assets::AssetHandle<assets::Prefab>(assets.at(id.data()).get(), id);
	}
};

inline auto uidOf(const char* s) -> uint64_t {
	return UID::fromString(s);
}

}    // namespace toast::tests
