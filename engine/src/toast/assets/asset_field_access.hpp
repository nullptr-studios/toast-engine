/**
 * @file asset_field_access.hpp
 * @author Xein
 *
 * @brief Reflection accessor for assets::AssetHandle<T> members
 *
 * @c get() returns the handle's UID and @c set() resolves a handle from the UID
 * via @c assets::load(), if it fails the handle will have a nnullptr but it will
 * still have the UID
 */

#pragma once

#include <any>
#include <toast/assets/assets.hpp>
#include <toast/uid.hpp>
#include <toast/world/reflect.hpp>

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

}
