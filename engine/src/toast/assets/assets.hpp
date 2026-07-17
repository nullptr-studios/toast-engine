/**
 * @file assets.hpp
 * @author Xein
 * @date 06 Jun 2026
 *
 * @brief Public API for the asset management system
 */

#pragma once

#include "core_types.hpp"

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

/**
 * @brief Looks up an asset's type string in the manifest without loading it
 * @return The manifest type
 */
auto TOAST_API typeOf(toast::UID uid) -> std::string;

auto TOAST_API save(toast::UID uid) -> bool;

/**
 * @brief Type-safe load helper
 */
template<typename T>
auto load(toast::UID uid) -> AssetHandle<T> {
	auto base = load(uid);
	// Carry the UID anyway, so the result is an unresolved handle (uid set, ptr null)
	return AssetHandle<T>(base.hasValue() ? &base.get() : nullptr, base.uid(), base.path());
}

template<typename T>
auto load(std::string_view uri) -> AssetHandle<T> {
	auto base = load(uri);
	return AssetHandle<T>(base.hasValue() ? &base.get() : nullptr, base.uid(), base.path());
}

}

namespace event {

/**
 * @brief Fired to request a refresh of the asset manifest
 */
struct ReloadAssetsManifest : public Event<ReloadAssetsManifest> { };

/**
 * @brief Fired after a hot-reload
 */
struct ScriptAssetReloaded : public Event<ScriptAssetReloaded> {
	toast::UID uid;

	explicit ScriptAssetReloaded(toast::UID uid) : uid(uid) { }
};

/**
 * @brief Fired after a shader source hot-reload
 */
struct ShaderAssetReloaded : public Event<ShaderAssetReloaded> {
	toast::UID uid;

	explicit ShaderAssetReloaded(toast::UID uid) : uid(uid) { }
};

/**
 * @brief Fired after a material or material instance hot-reload
 */
struct MaterialAssetReloaded : public Event<MaterialAssetReloaded> {
	toast::UID uid;

	explicit MaterialAssetReloaded(toast::UID uid) : uid(uid) { }
};

/**
 * @brief Fired to request the unloading of all unused assets
 */
struct ClearUnusedAssets : public Event<ClearUnusedAssets> { };

}
