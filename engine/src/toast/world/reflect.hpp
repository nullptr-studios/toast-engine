/**
 * @file Reflect.hpp
 * @author Xein
 * @date 24 May 2026
 *
 * @brief Runtime reflection system for Nodes, enabling serialization, ticking, and RTTI.
 */

#pragma once

#include <any>
#include <array>
#include <cstdint>
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
	uuid_t,
	vec2_t,
	vec3_t,
	vec4_t,
	quaternion_t,
};

// Bitwise operators for TickFunctionList
constexpr TickFunctionList operator&(TickFunctionList lhs, TickFunctionList rhs) {
	return static_cast<TickFunctionList>(static_cast<uint16_t>(lhs) & static_cast<uint16_t>(rhs));
}

constexpr TickFunctionList operator|(TickFunctionList lhs, TickFunctionList rhs) {
	return static_cast<TickFunctionList>(static_cast<uint16_t>(lhs) | static_cast<uint16_t>(rhs));
}

constexpr TickFunctionList& operator&=(TickFunctionList& lhs, TickFunctionList rhs) {
	lhs = lhs & rhs;
	return lhs;
}

constexpr TickFunctionList& operator|=(TickFunctionList& lhs, TickFunctionList rhs) {
	lhs = lhs | rhs;
	return lhs;
}

// Helper to check if a flag is set
constexpr bool has_flag(TickFunctionList flags, TickFunctionList flag) {
	return (flags & flag) == flag;
}

// Tick function invoker: calls a member function pointer on a Node instance
struct TickFunctions {
	using Invoker = void (*)(void*);    // Takes void* (Node*), invokes the appropriate member function

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
	std::string_view type;    // C++ type name, for diagnostics only
	FieldType value_type;     // serialization kind, drives NodeFile
	std::string_view attributes;
	bool is_array = false;    // True if field is array/vector/collection

	FieldGetterPtr get;
	FieldSetterPtr set;

	// Helper: extract the group name if this field has [[Group("name")]] attribute
	[[nodiscard]]
	auto groupName() const -> std::string_view {
		auto pos = attributes.find("Group");
		if (pos == std::string_view::npos) {
			return "";
		}
		auto start = attributes.find('"', pos);
		if (start == std::string_view::npos) {
			return "";
		}
		auto end = attributes.find('"', start + 1);
		if (end == std::string_view::npos) {
			return "";
		}
		return attributes.substr(start + 1, end - start - 1);
	}

	// Helper: check if attribute string contains a specific attribute name
	[[nodiscard]]
	auto hasAttribute(std::string_view attr_name) const -> bool {
		if (attributes.empty()) {
			return false;
		}
		auto pos = attributes.find(attr_name);
		while (pos != std::string_view::npos) {
			// Check if it's a whole word (not part of another attribute)
			bool valid_start = (pos == 0 || attributes[pos - 1] == ';');
			bool valid_end =
			    (pos + attr_name.length() >= attributes.length() || attributes[pos + attr_name.length()] == '(' ||
			     attributes[pos + attr_name.length()] == ';');
			if (valid_start && valid_end) {
				return true;
			}
			pos = attributes.find(attr_name, pos + 1);
		}
		return false;
	}

	// Helper: get attribute argument, e.g., getAttribute("Group") -> "General"
	[[nodiscard]]
	auto getAttribute(std::string_view attr_name) const -> std::string_view {
		auto pos = attributes.find(attr_name);
		if (pos == std::string_view::npos) {
			return "";
		}
		auto paren_start = attributes.find('(', pos);
		if (paren_start == std::string_view::npos) {
			return "";
		}
		auto quote_start = attributes.find('"', paren_start);
		if (quote_start == std::string_view::npos) {
			return "";
		}
		auto quote_end = attributes.find('"', quote_start + 1);
		if (quote_end == std::string_view::npos) {
			return "";
		}
		return attributes.substr(quote_start + 1, quote_end - quote_start - 1);
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

	// Construct/destroy an instance of this exact type (bodies generated, so they can
	// reach the private ctor/dtor). Needed to instantiate a node from its type name.
	Factory construct = nullptr;
	Deleter destroy = nullptr;

	// getField searches every field (grouped or not), then walks up to base types.
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

	// RTTI: is this type the same as, or derived from, `other`?
	[[nodiscard]]
	auto isA(const NodeInfo* other) const -> bool {
		for (const NodeInfo* cur = this; cur; cur = cur->base_type) {
			if (cur == other) {
				return true;
			}
		}
		return false;
	}

	// True if this type or any base registers any tick function overlapping `mask`.
	// Accepts a single flag (e.g. TickFunctionList::tick) or a mask (e.g. tick_mask).
	[[nodiscard]]
	auto hasFunction(TickFunctionList mask) const -> bool {
		for (const NodeInfo* cur = this; cur; cur = cur->base_type) {
			if ((cur->functions.list & mask) != TickFunctionList::none) {
				return true;
			}
		}
		return false;
	}

	// Stable numeric id derived from the type name (FNV-1a, 32-bit).
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

template<class Class, typename FieldType, FieldType Class::* MemberPtr>
struct FieldAccess {
	static auto get(void* obj) -> std::any { return static_cast<Class*>(obj)->*MemberPtr; }

	// Guarded: a value of the wrong stored type is ignored rather than throwing std::bad_any_cast.
	static void set(void* obj, std::any value) {
		if (auto* typed = std::any_cast<FieldType>(&value)) {
			static_cast<Class*>(obj)->*MemberPtr = *typed;
		}
	}
};

class TOAST_API NodeRegistry {
public:
	void registerNode(const NodeInfo* info) { types[info->type] = info; }

	[[nodiscard]]
	auto reflect(std::string_view name) const -> const NodeInfo* {
		auto it = types.find(name);
		return it != types.end() ? it->second : nullptr;
	}

private:
	std::unordered_map<std::string_view, const NodeInfo*> types;
};

void registerEngineTypes();

}
