/**
 * @file asset_field_access.hpp
 * @author Xein
 *
 * @brief Reflection accessor for assets::Handle<T> members
 *
 * @c get() returns the handle's UID and @c set() resolves a handle from the UID
 * via @c assets::load(), if it fails the handle will have a nnullptr but it will
 * still have the UID
 *
 * TOAST_API
 */

#pragma once

#include <any>
#include <toast/assets/assets.hpp>
#include <toast/reflect/reflect.hpp>
#include <toast/uid.hpp>
#include <vector>

namespace toast {

template<class Class, typename Handle, typename Tag>
struct AssetFieldAccess {
	static auto get(void* obj) -> std::any {
		return std::any {(static_cast<Class*>(obj)->*_detail::template Accessor<Tag>::member).uid()};
	}

	static void set(void* obj, std::any value) {
		if (auto* uid = std::any_cast<toast::UID>(&value)) {
			static_cast<Class*>(obj)->*_detail::template Accessor<Tag>::member = assets::load<typename Handle::asset_type>(*uid);
		}
	}
};

/**
 * @brief Reflection accessor for a std::vector of assets::Handle<T> members
 *
 * The reflection boundary exchanges a std::vector<UID>, matching how uid_t array fields are
 * serialized. @c get() collects each handle's UID; @c set() resolves a fresh handle per UID
 * via @c assets::load(), preserving the UID even when an asset fails to resolve
 */
template<class Class, typename Vector, typename Tag>
struct AssetArrayFieldAccess {
	using Handle = typename Vector::value_type;
	using AssetType = typename Handle::asset_type;

	static auto get(void* obj) -> std::any {
		const Vector& handles = static_cast<Class*>(obj)->*_detail::template Accessor<Tag>::member;
		std::vector<toast::UID> uids;
		uids.reserve(handles.size());
		for (const auto& handle : handles) {
			uids.push_back(handle.uid());
		}
		return std::any {std::move(uids)};
	}

	static void set(void* obj, std::any value) {
		if (auto* uids = std::any_cast<std::vector<toast::UID>>(&value)) {
			Vector handles;
			handles.reserve(uids->size());
			for (toast::UID uid : *uids) {
				handles.push_back(assets::load<AssetType>(uid));
			}
			static_cast<Class*>(obj)->*_detail::template Accessor<Tag>::member = std::move(handles);
		}
	}
};

}
