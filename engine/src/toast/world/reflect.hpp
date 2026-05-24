/**
 * @file Reflect.hpp
 * @author Xein
 * @date 24 May 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once

namespace toast {

enum class TickFunctions : uint16_t {
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
	all = 0xFFFF,
};

struct FieldInfo {
	std::string_view name;
	std::string_view type;
	std::any (*get)(void*);
	void (*set)(void*, std::any);
};

struct NodeInfo {
	std::string_view name;
	std::span<const FieldInfo> fields;

	// TODO: make this std::optional?
	[[nodiscard]]
	auto getField(std::string_view field_name) const -> const FieldInfo* {
		for (const auto& f : fields) {
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

class NodeRegistry {
public:
	static auto get() -> NodeRegistry& {
		static NodeRegistry instance;
		return instance;
	}

	void registerType(const NodeInfo* info) { types[info->name] = info; }

	auto reflect(std::string_view name) -> const NodeInfo* {
		auto it = types.find(name);
		return it != types.end() ? it->second : nullptr;
	}

private:
	std::unordered_map<std::string_view, const NodeInfo*> types;
};

void registerEngineTypes();

}
