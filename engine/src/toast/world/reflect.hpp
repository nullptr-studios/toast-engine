/**
 * @file Reflect.hpp
 * @author Xein
 * @date 24 May 2026
 *
 * @brief Runtime reflection system for Nodes
 */

#pragma once

#include <any>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <toast/export.hpp>
#include <unordered_map>

namespace toast {

class Node;

enum class TickFunctionList : uint16_t {
	none = 0,
	pre_init = 1 << 1,
	init = 1 << 2,
	destroy = 1 << 3,
	begin = 1 << 4,
	end = 1 << 5,
	on_enable = 1 << 6,
	on_disable = 1 << 7,
	early_tick = 1 << 8,
	tick = 1 << 9,
	post_physics = 1 << 10,
	late_tick = 1 << 11,
	load = 1 << 12,
	save = 1 << 13,
	tick_mask = early_tick | tick | post_physics | late_tick,
	all = 0xFFFF,
};

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

// Bitwise operators for TickFunctionList
constexpr auto operator&(TickFunctionList lhs, TickFunctionList rhs) -> TickFunctionList {
	return static_cast<TickFunctionList>(static_cast<uint16_t>(lhs) & static_cast<uint16_t>(rhs));
}

constexpr auto operator|(TickFunctionList lhs, TickFunctionList rhs) -> TickFunctionList {
	return static_cast<TickFunctionList>(static_cast<uint16_t>(lhs) | static_cast<uint16_t>(rhs));
}

constexpr auto operator&=(TickFunctionList& lhs, TickFunctionList rhs) -> TickFunctionList& {
	lhs = lhs & rhs;
	return lhs;
}

constexpr auto operator|=(TickFunctionList& lhs, TickFunctionList rhs) -> TickFunctionList& {
	lhs = lhs | rhs;
	return lhs;
}

// Helper to check if a flag is set
constexpr auto hasFlag(TickFunctionList flags, TickFunctionList flag) -> bool {
	return (flags & flag) == flag;
}

// Tick function invoker
struct TickFunctions {
	using Invoker = void (*)(void*);

	TickFunctionList list;

	Invoker load = nullptr;
	Invoker save = nullptr;
	Invoker pre_init = nullptr;
	Invoker init = nullptr;
	Invoker destroy = nullptr;
	Invoker begin = nullptr;
	Invoker end = nullptr;
	Invoker on_enable = nullptr;
	Invoker on_disable = nullptr;
	Invoker early_tick = nullptr;
	Invoker tick = nullptr;
	Invoker post_physics = nullptr;
	Invoker late_tick = nullptr;
};

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

	[[nodiscard]]
	auto groupName() const -> std::string {
		return getAttribute("Group");
	}

	[[nodiscard]]
	auto hasAttribute(std::string_view attr_name) const -> bool {
		return attributes.contains(std::string(attr_name));
	}

	[[nodiscard]]
	auto getAttribute(std::string_view attr_name) const -> std::string {
		auto it = attributes.find(std::string(attr_name));
		if (it == attributes.end() || it->empty()) {
			return "";
		}
		return it->at(0).get<std::string>();
	}
};

struct TOAST_API SubgroupInfo {
	std::string_view name;
	std::span<const FieldInfo* const> fields;

	[[nodiscard]]
	auto getField(std::string_view field_name) const -> const FieldInfo* {
		for (const auto& f : fields) {
			if (field_name == f->name) {
				return f;
			}
		}
		return nullptr;
	}
};

struct TOAST_API GroupInfo {
	std::string_view name;
	std::span<const FieldInfo* const> fields;
	std::span<const SubgroupInfo> subgroups;

	[[nodiscard]]
	auto getSubgroup(std::string_view group_name) const -> const SubgroupInfo* {
		for (const auto& g : subgroups) {
			if (group_name == g.name) {
				return &g;
			}
		}
		return nullptr;
	}

	[[nodiscard]]
	auto getField(std::string_view field_name) const -> const FieldInfo* {
		for (const auto& f : fields) {
			if (field_name == f->name) {
				return f;
			}
		}
		return nullptr;
	}
};

struct TOAST_API NodeInfo {
	using Factory = Node* (*)();
	using Deleter = void (*)(Node*);

	std::string_view type;
	const NodeInfo* base_type;
	std::span<const FieldInfo> all_fields;
	std::span<const FieldInfo* const> fields;
	std::span<const GroupInfo> groups;

	TickFunctions functions;

	Factory construct = nullptr;
	Deleter destroy = nullptr;

	[[nodiscard]]
	auto getField(std::string_view field_name) const -> const FieldInfo* {
		for (const auto& f : all_fields) {
			if (field_name == f.name) {
				return &f;
			}
		}
		// Check base type
		if (base_type) {
			return base_type->getField(field_name);
		}
		return nullptr;
	}

	[[nodiscard]]
	auto isA(const NodeInfo* other) const -> bool {
		for (const NodeInfo* cur = this; cur; cur = cur->base_type) {
			if (cur == other) {
				return true;
			}
		}
		return false;
	}

	[[nodiscard]]
	auto hasFunction(TickFunctionList mask) const -> bool {
		for (const NodeInfo* cur = this; cur; cur = cur->base_type) {
			if ((cur->functions.list & mask) != TickFunctionList::none) {
				return true;
			}
		}
		return false;
	}

	[[nodiscard]]
	constexpr auto id() const -> uint32_t {
		uint32_t hash = 2166136261u;
		for (char c : type) {
			hash ^= static_cast<uint8_t>(c);
			hash *= 16777619u;
		}
		return hash;
	}

	[[nodiscard]]
	auto getGroup(std::string_view group_name) const -> const GroupInfo* {
		for (const auto& g : groups) {
			if (group_name == g.name) {
				return &g;
			}
		}
		// Check base type
		if (base_type) {
			return base_type->getGroup(group_name);
		}
		return nullptr;
	}

	[[nodiscard]]
	auto search(std::string_view field_name) const -> const FieldInfo* {
		for (const auto& f : all_fields) {
			if (field_name == f.name) {
				return &f;
			}
		}
		// Check base type
		if (base_type) {
			return base_type->search(field_name);
		}
		return nullptr;
	}

	// Walk the inheritance chain and call fn for each NodeInfo (base → derived)
	template<typename F>
	void forEachBaseType(F&& fn) const {
		if (base_type) {
			base_type->forEachBaseType(fn);
		}
		fn(*this);
	}
};

template<class T>
class Reflect;

namespace _reflect_impl {
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

template<class Class, typename FieldType, typename Tag>
struct FieldAccess {
	static auto get(void* obj) -> std::any { return static_cast<Class*>(obj)->*_reflect_impl::template Accessor<Tag>::member; }

	static void set(void* obj, std::any value) {
		if (auto* typed = std::any_cast<FieldType>(&value)) {
			static_cast<Class*>(obj)->*_reflect_impl::template Accessor<Tag>::member = *typed;
		}
	}
};

class TOAST_API NodeRegistry {
public:
	NodeRegistry() { instance = this; }

	static void registerNode(const NodeInfo* info) { (*instance).types[info->type] = info; }

	[[nodiscard]]
	static auto reflect(std::string_view name) -> const NodeInfo* {
		auto it = (*instance).types.find(name);
		return it != (*instance).types.end() ? it->second : nullptr;
	}

private:
	std::unordered_map<std::string_view, const NodeInfo*> types;
	static inline NodeRegistry* instance = nullptr;
};

void registerEngineTypes();

}
