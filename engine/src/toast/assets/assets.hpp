/**
 * @file assets.hpp
 * @author Xein
 * @date 06 Jun 2026
 *
 * @brief Public API for the asset management system
 */

#pragma once

#include "types.hpp"

#include <atomic>
#include <string_view>
#include <toast/events/event.hpp>
#include <toast/export.hpp>
#include <toast/uid.hpp>

namespace assets {

// Public load functions
auto TOAST_API load(toast::UID uid) -> AssetHandleBase;
auto TOAST_API load(std::string_view uri) -> AssetHandleBase;
auto TOAST_API resolveURI(std::string_view uri) -> std::optional<toast::UID>;

/**
 * @brief Lists the UIDs of every manifest asset of a given type
 * @param type The asset type string, e.g. "input_action"
 * @return The UIDs tracked by the manifest for that type; empty when none exist
 */
auto TOAST_API listByType(std::string_view type) -> std::vector<toast::UID>;

auto TOAST_API save(toast::UID uid) -> bool;

/**
 * @brief Type-safe load helper
 */
template<typename T>
auto load(toast::UID uid) -> AssetHandle<T> {
	auto base = load(uid);
	// Carry the UID anyway, so the result is an unresolved handle (uid set, ptr null)
	return AssetHandle<T>(base.hasValue() ? &base.get() : nullptr, base.uid());
}

template<typename T>
auto load(std::string_view uri) -> AssetHandle<T> {
	auto base = load(uri);
	return AssetHandle<T>(base.hasValue() ? &base.get() : nullptr, base.uid());
}

}

namespace event {

/**
 * @brief Fired to request a refresh of the asset manifest
 */
struct ReloadAssetsManifest : public Event<ReloadAssetsManifest> { };

/**
 * @brief Fired to request the unloading of all unused assets
 */
struct ClearUnusedAssets : public Event<ClearUnusedAssets> { };

}
