/**
 * @file Reflect.hpp
 * @author Xein
 * @date 24 May 2026
 *
 * @brief Runtime reflection system for Nodes, enabling serialization, ticking, and RTTI.
 */

#pragma once

#include <any>
#include <toast/export.hpp>

namespace toast {

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
	// load = 1 << 12,
	// save = 1 << 13,
	tick_mask = early_tick | tick | post_physics | late_tick,
	all = 0xFFFF,
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
	std::string_view type;
	std::string_view attributes;

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
	std::string_view type;
	NodeInfo* base_type;
	std::span<const FieldInfo> all_fields;
	std::span<const FieldInfo* const> fields;
	std::span<const GroupInfo> groups;

	TickFunctions functions;

	[[nodiscard]]
	auto getField(std::string_view field_name) const -> const FieldInfo* {
		for (const auto& f : fields) {
			if (field_name == f->name) {
				return f;
			}
		}
		return nullptr;
	}

	[[nodiscard]]
	auto getGroup(std::string_view group_name) const -> const GroupInfo* {
		for (const auto& g : groups) {
			if (group_name == g.name) {
				return &g;
			}
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
		return nullptr;
	}
};

template<class T>
class Reflect;

template<class Class, typename FieldType, FieldType Class::* MemberPtr>
struct FieldAccess {
	static auto get(void* obj) -> std::any { return static_cast<Class*>(obj)->*MemberPtr; }

	static void set(void* obj, std::any value) { static_cast<Class*>(obj)->*MemberPtr = std::any_cast<FieldType>(value); }
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
