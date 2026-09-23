/// @file shader_reflection.hpp
/// @author Xein
/// @date 17/07/2026

#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <slang.h>
#include <string>
#include <toast/export.hpp>
#include <vector>

namespace renderer {

enum class ShaderMemberType : uint8_t {
	unknown,
	bool_t,
	int_t,
	uint_t,
	float_t,
	vec2,
	vec3,
	vec4,
	mat3,
	mat4,
};

enum class ShaderBindingKind : uint8_t {
	uniform_buffer,
	storage_buffer,
	combined_image_sampler,
	sampled_image,
	sampler,
	storage_image,
	acceleration_structure,
};

struct ShaderInspectorMeta {
	bool reflected = false;
	std::optional<float> range_min;
	std::optional<float> range_max;
	bool is_color = false;
	std::string display_name;
	std::string group;
	std::string subgroup;
	std::string unit;
	std::string default_fallback;    // "white" "black" or "flat_normal"
	bool linear_data = false;
};

struct ShaderBlockMember {
	std::string name;
	ShaderMemberType type = ShaderMemberType::unknown;
	uint32_t offset = 0;
	uint32_t size = 0;
	uint32_t element_count = 0;
	uint32_t element_stride = 0;
	std::string engine_semantic;
	ShaderInspectorMeta inspector;
};

struct ShaderBinding {
	uint32_t set = 0;
	uint32_t binding = 0;
	std::string name;
	ShaderBindingKind kind = ShaderBindingKind::uniform_buffer;
	uint32_t count = 1;
	uint32_t size = 0;
	std::vector<ShaderBlockMember> members;
	std::string engine_semantic;    // "frame" for engine reserved set 0
	ShaderInspectorMeta inspector;
};

struct ShaderPushConstants {
	std::string name;
	uint32_t size = 0;
	std::vector<ShaderBlockMember> members;
};

struct ShaderEntryPoint {
	std::string name;
	std::string stage;    // "vertex" "fragment" or "compute"
};

struct ShaderReflection {
	std::vector<ShaderEntryPoint> entry_points;
	std::vector<ShaderBinding> bindings;
	std::vector<ShaderPushConstants> push_constants;
	std::vector<std::string> layout_order;

	[[nodiscard]]
	auto TOAST_API toJson() const -> nlohmann::json;

	static auto TOAST_API fromJson(const nlohmann::json& json) -> std::optional<ShaderReflection>;
};

auto extractReflection(slang::ProgramLayout* layout) -> ShaderReflection;

/// Must come from the IModule since ProgramLayout reports 0 entry points for a module only composite
void extractModuleEntryPoints(slang::IModule* module, ShaderReflection& reflection);

auto toString(ShaderMemberType type) -> std::string_view;
auto toString(ShaderBindingKind kind) -> std::string_view;
auto memberTypeFromString(std::string_view str) -> ShaderMemberType;
auto bindingKindFromString(std::string_view str) -> ShaderBindingKind;

}
