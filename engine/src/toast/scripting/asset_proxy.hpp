/**
 * @file asset_proxy.hpp
 * @author Xein
 * @date 11 Jul 2026
 * @brief Lua-side proxy for an asset handle, registered as "Asset" in Lua
 */

#pragma once

#include <string>
#include <toast/assets/core_types.hpp>
#include <toast/uid.hpp>

namespace scripting {

class AssetProxy {
public:
	AssetProxy() = default;
	explicit AssetProxy(toast::UID uid);                    ///< Create from UID
	explicit AssetProxy(assets::AssetHandleBase handle);    ///< Create from another handle

	[[nodiscard]]
	auto path() const -> std::string;
	[[nodiscard]]
	auto uid() const -> toast::UID;
	[[nodiscard]]
	auto hasValue() const -> bool;
	[[nodiscard]]
	auto type() const -> std::string;
	[[nodiscard]]
	auto toString() const -> std::string;
	[[nodiscard]]
	auto checkType(std::string_view field_type) const -> std::string;

	[[nodiscard]]
	auto handle() const noexcept -> const assets::AssetHandleBase& {
		return m_handle;
	}

private:
	assets::AssetHandleBase m_handle;
};

}
