/**
 * @file Reflect.hpp
 * @author Xein
 * @date 24 May 2026
 *
 * @brief Runtime reflection classes
 */

#pragma once

#include <any>
#include <cassert>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <toast/export.hpp>
#include <toast/log.hpp>
#include <toast/world/box.hpp>
#include <typeinfo>

namespace toast {

/**
 * @brief Serialization kind for a reflected field
 *
 * Determines how the generated accessor reads and writes the field value across the
 * FFI boundary and how the inspector widget renders the field.
 */
enum class FieldType : uint8_t {
	bool_t,
	int_t,
	float_t,
	string_t,
	double_t,
	uid_t,
	vec2_t,
	vec3_t,
	vec4_t,
	quaternion_t,
};

/**
 * @brief Runtime descriptor for one reflected field
 *
 * Holds type metadata, JSON attributes parsed from the header annotation (e.g. Group, Name),
 * and a type-erased getter/setter pair that works without knowing the concrete Node subtype.
 *
 * @note The getter returns std::any; the setter silently does nothing on a type mismatch
 */
struct TOAST_API FieldInfo {
	using FieldGetterPtr = std::any (*)(void*);
	using FieldSetterPtr = void (*)(void*, std::any);

	std::string_view name;
	std::string_view type;    // C++ type name
	FieldType value_type;     // serialization kind
	nlohmann::json attributes;
	bool is_array = false;    // True if field is vector

	FieldGetterPtr get;
	FieldSetterPtr set;

	/// Returns the value of the "Group" annotation, or an empty string if not present
	[[nodiscard]]
	auto groupName() const -> std::string {
		return getAttribute("Group");
	}

	/**
	 * @brief Checks whether a JSON annotation key is present on this field
	 * @param attr_name The annotation key, e.g. "Group" or "Name"
	 * @return true if the attribute exists, regardless of its value
	 */
	[[nodiscard]]
	auto hasAttribute(std::string_view attr_name) const -> bool {
		return attributes.contains(std::string(attr_name));
	}

	/**
	 * @brief Returns the first string value of a JSON annotation
	 * @param attr_name The annotation key to look up
	 * @return The first string element, or an empty string if the key is absent or has no elements
	 */
	[[nodiscard]]
	auto getAttribute(std::string_view attr_name) const -> std::string {
		auto it = attributes.find(std::string(attr_name));
		if (it == attributes.end() || it->empty()) {
			return "";
		}
		return it->at(0).get<std::string>();
	}
};

/**
 * @brief Runtime descriptor for one parameter of a reflected function
 */
struct TOAST_API ParameterInfo {
	std::string_view name;
	std::string_view type;            // C++ type name, as spelled in the header
	std::optional<std::string_view> default_value;
	const std::type_info* type_id;    // typeid(std::decay_t<param>), for call-time validation
};

/**
 * @brief Runtime descriptor for one reflected member function
 *
 * @note Do not call invoke directly; go through NodeInfo::call()
 */
struct TOAST_API FunctionInfo {
	/// Generic erased trampoline pointer; reinterpret_cast back to the real signature before calling
	using Invoker = void (*)();

	std::string_view name;
	std::string_view return_type;            // C++ return type name, as spelled in the header
	const std::type_info* return_type_id;    // typeid(std::decay_t<return>), for call-time validation
	std::span<const ParameterInfo> parameters;
	Invoker invoke = nullptr;

	template<typename R = void, typename... Args>
	auto call(void* obj, Args&&... args) const -> R {
		bool signature_ok =
		    parameters.size() == sizeof...(Args) && return_type_id != nullptr && *return_type_id == typeid(std::decay_t<R>);
		if (signature_ok) {
			const std::array<const std::type_info*, sizeof...(Args)> expected = {&typeid(std::decay_t<Args>)...};
			for (std::size_t i = 0; i < expected.size(); ++i) {
				if (parameters[i].type_id == nullptr || *parameters[i].type_id != *expected[i]) {
					signature_ok = false;
					break;
				}
			}
		}
		if (!signature_ok) {
			TOAST_WARN("Reflect", "call(): signature mismatch for '{}'; invoking is undefined behavior", name);
			assert(false && "FunctionInfo::call(): argument or return type mismatch");
		}

		using Trampoline = R (*)(void*, std::decay_t<Args>...);
		auto* trampoline = reinterpret_cast<Trampoline>(invoke);
		return trampoline(obj, std::forward<Args>(args)...);
	}
};

template<class T>
class Reflect;

/**
 * @brief Member-robber pattern used by the generated reflection code
 *
 * A Robber<Tag,Ptr> static instance runs at startup and writes the private member pointer Ptr
 * into the public Accessor<Tag>::member. This is the only standards-conforming way to obtain a
 * pointer-to-private-member at compile time without modifying the Node class header.
 *
 * @note This namespace is an implementation detail; user code should never reference it directly
 */
namespace _detail {
template<typename Tag>
struct Accessor {
	inline static typename Tag::type member;
};

template<typename Tag, typename Tag::type Ptr>
struct Robber {
	Robber() { Accessor<Tag>::member = Ptr; }

	static Robber instance;
};

template<typename Tag, typename Tag::type Ptr>
Robber<Tag, Ptr> Robber<Tag, Ptr>::instance;
}

/**
 * @brief Generated getter/setter pair binding a private member pointer to the FieldInfo interface
 *
 * Produced by the reflection code generator using the Robber pattern in toast::_detail.
 * Allows the inspector and serialization layer to read and write private fields without
 * modifying the Node class header.
 *
 * @tparam Class The Node subtype that owns the field
 * @tparam FieldType The C++ type of the field
 * @tparam Tag Tag type that holds the pointer-to-member via Accessor<Tag>::member
 */
template<class Class, typename FieldType, typename Tag>
struct FieldAccess {
	static auto get(void* obj) -> std::any { return static_cast<Class*>(obj)->*_detail::template Accessor<Tag>::member; }

	static void set(void* obj, std::any value) {
		if (auto* typed = std::any_cast<FieldType>(&value)) {
			static_cast<Class*>(obj)->*_detail::template Accessor<Tag>::member = *typed;
			return;
		}

		// HACK: Try integer type conversion if exact match fails
		if constexpr (std::is_integral_v<FieldType>) {
			const auto& type_info = value.type();
			if (type_info == typeid(int)) {
				static_cast<Class*>(obj)->*_detail::template Accessor<Tag>::member = static_cast<FieldType>(std::any_cast<int>(value));
			} else if (type_info == typeid(unsigned int)) {
				static_cast<Class*>(obj)->*_detail::template Accessor<Tag>::member =
				    static_cast<FieldType>(std::any_cast<unsigned int>(value));
			} else if (type_info == typeid(long)) {
				static_cast<Class*>(obj)->*_detail::template Accessor<Tag>::member = static_cast<FieldType>(std::any_cast<long>(value));
			} else if (type_info == typeid(unsigned long)) {
				static_cast<Class*>(obj)->*_detail::template Accessor<Tag>::member =
				    static_cast<FieldType>(std::any_cast<unsigned long>(value));
			} else if (type_info == typeid(short)) {
				static_cast<Class*>(obj)->*_detail::template Accessor<Tag>::member = static_cast<FieldType>(std::any_cast<short>(value));
			} else if (type_info == typeid(unsigned short)) {
				static_cast<Class*>(obj)->*_detail::template Accessor<Tag>::member =
				    static_cast<FieldType>(std::any_cast<unsigned short>(value));
			} else if (type_info == typeid(char)) {
				static_cast<Class*>(obj)->*_detail::template Accessor<Tag>::member = static_cast<FieldType>(std::any_cast<char>(value));
			} else if (type_info == typeid(unsigned char)) {
				static_cast<Class*>(obj)->*_detail::template Accessor<Tag>::member =
				    static_cast<FieldType>(std::any_cast<unsigned char>(value));
			}
		}
	}
};

/**
 * @brief Partial specialization of FieldAccess for Box<T> fields
 *
 * Widens the stored type to Box<Node> in both directions so that the scripting layer can
 * handle all node-reference fields uniformly, without needing per-derived-type converters
 */
template<class Class, typename T, typename Tag>
struct FieldAccess<Class, Box<T>, Tag> {
	static auto get(void* obj) -> std::any {
		Box<T>& src = static_cast<Class*>(obj)->*_detail::template Accessor<Tag>::member;
		return std::any {Box<Node>(src)};
	}

	static void set(void* obj, std::any value) {
		if (auto* box = std::any_cast<Box<Node>>(&value)) {
			static_cast<Class*>(obj)->*_detail::template Accessor<Tag>::member = box->as<T>();
		}
	}
};

}
