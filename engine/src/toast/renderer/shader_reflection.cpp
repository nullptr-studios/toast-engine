/// @file shader_reflection.cpp
/// @author Xein
/// @date 17/07/2026.

#include "shader_reflection.hpp"

#include <toast/log.hpp>

namespace renderer {

namespace {

auto stageToString(SlangStage stage) -> std::string_view {
	switch (stage) {
		case SLANG_STAGE_VERTEX: return "vertex";
		case SLANG_STAGE_FRAGMENT: return "fragment";
		case SLANG_STAGE_COMPUTE: return "compute";
		case SLANG_STAGE_GEOMETRY: return "geometry";
		case SLANG_STAGE_HULL: return "hull";
		case SLANG_STAGE_DOMAIN: return "domain";
		default: return "unknown";
	}
}

auto mapMemberType(slang::TypeLayoutReflection* type_layout) -> ShaderMemberType {
	if (type_layout == nullptr) {
		return ShaderMemberType::unknown;
	}

	auto* type = type_layout->getType();
	if (type == nullptr) {
		return ShaderMemberType::unknown;
	}

	const auto kind = type_layout->getKind();

	if (kind == slang::TypeReflection::Kind::Scalar) {
		switch (type->getScalarType()) {
			case slang::TypeReflection::ScalarType::Bool: return ShaderMemberType::bool_t;
			case slang::TypeReflection::ScalarType::Int32: return ShaderMemberType::int_t;
			case slang::TypeReflection::ScalarType::UInt32: return ShaderMemberType::uint_t;
			case slang::TypeReflection::ScalarType::Float32: return ShaderMemberType::float_t;
			default: return ShaderMemberType::unknown;
		}
	}

	if (kind == slang::TypeReflection::Kind::Vector) {
		switch (type->getElementCount()) {
			case 2: return ShaderMemberType::vec2;
			case 3: return ShaderMemberType::vec3;
			case 4: return ShaderMemberType::vec4;
			default: return ShaderMemberType::unknown;
		}
	}

	if (kind == slang::TypeReflection::Kind::Matrix) {
		if (type->getRowCount() == 3 && type->getColumnCount() == 3) {
			return ShaderMemberType::mat3;
		}
		if (type->getRowCount() == 4 && type->getColumnCount() == 4) {
			return ShaderMemberType::mat4;
		}
		return ShaderMemberType::unknown;
	}

	return ShaderMemberType::unknown;
}

auto extractBlockMembers(slang::TypeLayoutReflection* struct_layout) -> std::vector<ShaderBlockMember> {
	std::vector<ShaderBlockMember> members;
	if (struct_layout == nullptr || struct_layout->getKind() != slang::TypeReflection::Kind::Struct) {
		return members;
	}

	const uint32_t field_count = struct_layout->getFieldCount();
	members.reserve(field_count);
	for (uint32_t i = 0; i < field_count; ++i) {
		auto* field = struct_layout->getFieldByIndex(i);
		if (field == nullptr) {
			continue;
		}

		ShaderBlockMember member;
		member.name = field->getName() != nullptr ? field->getName() : "";
		member.offset = static_cast<uint32_t>(field->getOffset(SLANG_PARAMETER_CATEGORY_UNIFORM));
		if (auto* field_type = field->getTypeLayout()) {
			member.size = static_cast<uint32_t>(field_type->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM));
			member.type = mapMemberType(field_type);
		}

		// The engine writes the model matrix itself; everything else is material data
		if (member.name == "model" && member.type == ShaderMemberType::mat4) {
			member.engine_semantic = "model_matrix";
		}

		members.push_back(std::move(member));
	}
	return members;
}

auto mapBindingKind(slang::TypeLayoutReflection* type_layout) -> std::optional<ShaderBindingKind> {
	if (type_layout == nullptr) {
		return std::nullopt;
	}

	switch (type_layout->getKind()) {
		case slang::TypeReflection::Kind::ConstantBuffer: return ShaderBindingKind::uniform_buffer;
		case slang::TypeReflection::Kind::ShaderStorageBuffer: return ShaderBindingKind::storage_buffer;
		case slang::TypeReflection::Kind::SamplerState: return ShaderBindingKind::sampler;
		case slang::TypeReflection::Kind::Resource: break;
		default: return std::nullopt;
	}

	const auto shape = type_layout->getResourceShape();
	const auto access = type_layout->getResourceAccess();
	const auto base_shape = shape & SLANG_RESOURCE_BASE_SHAPE_MASK;

	switch (base_shape) {
		case SLANG_TEXTURE_1D:
		case SLANG_TEXTURE_2D:
		case SLANG_TEXTURE_3D:
		case SLANG_TEXTURE_CUBE:
			if (access == SLANG_RESOURCE_ACCESS_READ_WRITE) {
				return ShaderBindingKind::storage_image;
			}
			if ((shape & SLANG_TEXTURE_COMBINED_FLAG) != 0) {
				return ShaderBindingKind::combined_image_sampler;
			}
			return ShaderBindingKind::sampled_image;
		case SLANG_STRUCTURED_BUFFER:
		case SLANG_BYTE_ADDRESS_BUFFER: return ShaderBindingKind::storage_buffer;
		default: return std::nullopt;
	}
}

auto isPushConstant(slang::VariableLayoutReflection* var_layout) -> bool {
	const uint32_t category_count = var_layout->getCategoryCount();
	for (uint32_t c = 0; c < category_count; ++c) {
		if (var_layout->getCategoryByIndex(c) == slang::ParameterCategory::PushConstantBuffer) {
			return true;
		}
	}
	return false;
}

}

auto extractReflection(slang::ProgramLayout* layout) -> ShaderReflection {
	ShaderReflection reflection;
	if (layout == nullptr) {
		return reflection;
	}

	const uint32_t entry_point_count = layout->getEntryPointCount();
	reflection.entry_points.reserve(entry_point_count);
	for (uint32_t i = 0; i < entry_point_count; ++i) {
		auto* entry = layout->getEntryPointByIndex(i);
		if (entry == nullptr) {
			continue;
		}
		reflection.entry_points.push_back(
		    ShaderEntryPoint {
		      .name = entry->getName() != nullptr ? entry->getName() : "",
		      .stage = std::string(stageToString(entry->getStage())),
		    }
		);
	}

	const uint32_t parameter_count = layout->getParameterCount();
	for (uint32_t i = 0; i < parameter_count; ++i) {
		auto* var_layout = layout->getParameterByIndex(i);
		if (var_layout == nullptr) {
			continue;
		}

		const std::string name = var_layout->getName() != nullptr ? var_layout->getName() : "";
		auto* type_layout = var_layout->getTypeLayout();
		if (type_layout == nullptr) {
			continue;
		}

		if (isPushConstant(var_layout)) {
			ShaderPushConstants push;
			push.name = name;

			auto* element_layout = type_layout->getKind() == slang::TypeReflection::Kind::ConstantBuffer
			                           ? type_layout->getElementTypeLayout()
			                           : type_layout;
			if (element_layout != nullptr) {
				push.size = static_cast<uint32_t>(element_layout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM));
				push.members = extractBlockMembers(element_layout);
			}

			if (push.size > 0) {
				reflection.layout_order.push_back(name);
				reflection.push_constants.push_back(std::move(push));
			}
			continue;
		}

		const auto kind = mapBindingKind(type_layout);
		if (!kind.has_value()) {
			TOAST_WARN("Vulkan", "Shader reflection skipped parameter '{}' with unsupported type", name);
			continue;
		}

		ShaderBinding binding;
		binding.name = name;
		binding.kind = *kind;
		binding.binding = static_cast<uint32_t>(var_layout->getOffset(slang::ParameterCategory::DescriptorTableSlot));
		binding.set = static_cast<uint32_t>(var_layout->getBindingSpace(slang::ParameterCategory::DescriptorTableSlot));

		if (binding.kind == ShaderBindingKind::uniform_buffer) {
			if (auto* element_layout = type_layout->getElementTypeLayout()) {
				binding.size = static_cast<uint32_t>(element_layout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM));
				binding.members = extractBlockMembers(element_layout);
			}
		}

		// Set 0 is engine-reserved frame data (camera etc), never material-editable
		if (binding.set == 0) {
			binding.engine_semantic = "frame";
		}

		reflection.layout_order.push_back(name);
		reflection.bindings.push_back(std::move(binding));
	}

	return reflection;
}

auto toString(ShaderMemberType type) -> std::string_view {
	switch (type) {
		case ShaderMemberType::bool_t: return "bool";
		case ShaderMemberType::int_t: return "int";
		case ShaderMemberType::uint_t: return "uint";
		case ShaderMemberType::float_t: return "float";
		case ShaderMemberType::vec2: return "vec2";
		case ShaderMemberType::vec3: return "vec3";
		case ShaderMemberType::vec4: return "vec4";
		case ShaderMemberType::mat3: return "mat3";
		case ShaderMemberType::mat4: return "mat4";
		default: return "unknown";
	}
}

auto toString(ShaderBindingKind kind) -> std::string_view {
	switch (kind) {
		case ShaderBindingKind::uniform_buffer: return "uniform_buffer";
		case ShaderBindingKind::storage_buffer: return "storage_buffer";
		case ShaderBindingKind::combined_image_sampler: return "combined_image_sampler";
		case ShaderBindingKind::sampled_image: return "sampled_image";
		case ShaderBindingKind::sampler: return "sampler";
		case ShaderBindingKind::storage_image: return "storage_image";
	}
	return "uniform_buffer";
}

auto memberTypeFromString(std::string_view str) -> ShaderMemberType {
	if (str == "bool") {
		return ShaderMemberType::bool_t;
	}
	if (str == "int") {
		return ShaderMemberType::int_t;
	}
	if (str == "uint") {
		return ShaderMemberType::uint_t;
	}
	if (str == "float") {
		return ShaderMemberType::float_t;
	}
	if (str == "vec2") {
		return ShaderMemberType::vec2;
	}
	if (str == "vec3") {
		return ShaderMemberType::vec3;
	}
	if (str == "vec4") {
		return ShaderMemberType::vec4;
	}
	if (str == "mat3") {
		return ShaderMemberType::mat3;
	}
	if (str == "mat4") {
		return ShaderMemberType::mat4;
	}
	return ShaderMemberType::unknown;
}

auto bindingKindFromString(std::string_view str) -> ShaderBindingKind {
	if (str == "storage_buffer") {
		return ShaderBindingKind::storage_buffer;
	}
	if (str == "combined_image_sampler") {
		return ShaderBindingKind::combined_image_sampler;
	}
	if (str == "sampled_image") {
		return ShaderBindingKind::sampled_image;
	}
	if (str == "sampler") {
		return ShaderBindingKind::sampler;
	}
	if (str == "storage_image") {
		return ShaderBindingKind::storage_image;
	}
	return ShaderBindingKind::uniform_buffer;
}

namespace {

auto membersToJson(const std::vector<ShaderBlockMember>& members) -> nlohmann::json {
	auto json = nlohmann::json::array();
	for (const auto& member : members) {
		nlohmann::json m {
		  {  "name",           member.name},
		  {  "type", toString(member.type)},
		  {"offset",         member.offset},
		  {  "size",           member.size},
		};
		if (!member.engine_semantic.empty()) {
			m["engine_semantic"] = member.engine_semantic;
		}
		json.push_back(std::move(m));
	}
	return json;
}

auto membersFromJson(const nlohmann::json& json) -> std::vector<ShaderBlockMember> {
	std::vector<ShaderBlockMember> members;
	for (const auto& m : json) {
		ShaderBlockMember member;
		member.name = m.value("name", "");
		member.type = memberTypeFromString(m.value("type", "unknown"));
		member.offset = m.value("offset", 0u);
		member.size = m.value("size", 0u);
		member.engine_semantic = m.value("engine_semantic", "");
		members.push_back(std::move(member));
	}
	return members;
}

}

auto ShaderReflection::toJson() const -> nlohmann::json {
	nlohmann::json json;

	auto entry_points_json = nlohmann::json::array();
	for (const auto& entry : entry_points) {
		entry_points_json.push_back({
		  { "name",  entry.name},
      {"stage", entry.stage}
		});
	}
	json["entry_points"] = std::move(entry_points_json);

	auto bindings_json = nlohmann::json::array();
	for (const auto& binding : bindings) {
		nlohmann::json b {
		  {    "set",		                binding.set},
		  {"binding",                binding.binding},
		  {   "name",		               binding.name},
		  {   "kind",         toString(binding.kind)},
		  {  "count",		              binding.count},
		  {   "size",		               binding.size},
		  {"members", membersToJson(binding.members)},
		};
		if (!binding.engine_semantic.empty()) {
			b["engine_semantic"] = binding.engine_semantic;
		}
		bindings_json.push_back(std::move(b));
	}
	json["bindings"] = std::move(bindings_json);

	auto push_json = nlohmann::json::array();
	for (const auto& push : push_constants) {
		push_json.push_back({
		  {   "name",		               push.name},
		  {   "size",		               push.size},
		  {"members", membersToJson(push.members)},
		});
	}
	json["push_constants"] = std::move(push_json);
	json["layout_order"] = layout_order;

	return json;
}

auto ShaderReflection::fromJson(const nlohmann::json& json) -> std::optional<ShaderReflection> {
	if (!json.is_object()) {
		return std::nullopt;
	}

	try {
		ShaderReflection reflection;

		for (const auto& entry : json.value("entry_points", nlohmann::json::array())) {
			reflection.entry_points.push_back(
			    ShaderEntryPoint {
			      .name = entry.value("name", ""),
			      .stage = entry.value("stage", ""),
			    }
			);
		}

		for (const auto& b : json.value("bindings", nlohmann::json::array())) {
			ShaderBinding binding;
			binding.set = b.value("set", 0u);
			binding.binding = b.value("binding", 0u);
			binding.name = b.value("name", "");
			binding.kind = bindingKindFromString(b.value("kind", "uniform_buffer"));
			binding.count = b.value("count", 1u);
			binding.size = b.value("size", 0u);
			binding.engine_semantic = b.value("engine_semantic", "");
			binding.members = membersFromJson(b.value("members", nlohmann::json::array()));
			reflection.bindings.push_back(std::move(binding));
		}

		for (const auto& p : json.value("push_constants", nlohmann::json::array())) {
			ShaderPushConstants push;
			push.name = p.value("name", "");
			push.size = p.value("size", 0u);
			push.members = membersFromJson(p.value("members", nlohmann::json::array()));
			reflection.push_constants.push_back(std::move(push));
		}

		reflection.layout_order = json.value("layout_order", std::vector<std::string> {});

		return reflection;
	} catch (const nlohmann::json::exception&) { return std::nullopt; }
}

}
