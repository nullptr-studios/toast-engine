#include "material_runtime.hpp"

#include "vulkan_core.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <glm/glm.hpp>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>

namespace renderer {

using assets::DataType;
using assets::DataValue;

namespace {

void readFloats(const DataValue& v, float* out, size_t count) {
	switch (v.type()) {
		case DataType::float_t:
		case DataType::int_t:
			if (count >= 1) {
				out[0] = static_cast<float>(v.value<double>().value_or(0.0));
			}
			return;
		case DataType::vec2_t: {
			const auto vec = v.as<glm::vec2>();
			for (size_t i = 0; i < std::min<size_t>(count, 2); ++i) {
				out[i] = vec[static_cast<int>(i)];
			}
			return;
		}
		case DataType::vec3_t:
		case DataType::color3_t: {
			const auto vec = v.as<glm::vec3>();
			for (size_t i = 0; i < std::min<size_t>(count, 3); ++i) {
				out[i] = vec[static_cast<int>(i)];
			}
			return;
		}
		case DataType::color4_t: {
			const auto vec = v.as<glm::vec4>();
			for (size_t i = 0; i < std::min<size_t>(count, 4); ++i) {
				out[i] = vec[static_cast<int>(i)];
			}
			return;
		}
		default: break;
	}

	if (v.isArray()) {
		const size_t n = std::min<size_t>(count, v.size());
		for (size_t i = 0; i < n; ++i) {
			out[i] = static_cast<float>(v[i].value<double>().value_or(0.0));
		}
	}
}

auto scalarSize(ShaderMemberType type) -> size_t {
	switch (type) {
		case ShaderMemberType::float_t:
		case ShaderMemberType::int_t:
		case ShaderMemberType::uint_t:
		case ShaderMemberType::bool_t: return 4;
		case ShaderMemberType::vec2: return 8;
		case ShaderMemberType::vec3: return 12;
		case ShaderMemberType::vec4: return 16;
		default: return 0;
	}
}

void writeScalar(
    std::vector<std::byte>& blob, ShaderMemberType type, uint32_t offset, const ShaderInspectorMeta& meta, const DataValue& v
) {
	const size_t size = scalarSize(type);
	if (size == 0 || offset + size > blob.size()) {
		return;
	}
	std::byte* dst = blob.data() + offset;

	// [Range(min, max)] clamps numeric values engine-side too
	const auto clamped = [&meta](double value) {
		if (meta.range_min.has_value()) {
			value = std::max(value, static_cast<double>(*meta.range_min));
		}
		if (meta.range_max.has_value()) {
			value = std::min(value, static_cast<double>(*meta.range_max));
		}
		return value;
	};

	switch (type) {
		case ShaderMemberType::float_t: {
			const auto value = static_cast<float>(clamped(v.value<double>().value_or(0.0)));
			std::memcpy(dst, &value, sizeof(value));
			return;
		}
		case ShaderMemberType::int_t: {
			const auto value = static_cast<int32_t>(clamped(static_cast<double>(v.value<int64_t>().value_or(0))));
			std::memcpy(dst, &value, sizeof(value));
			return;
		}
		case ShaderMemberType::uint_t: {
			const auto value = static_cast<uint32_t>(clamped(static_cast<double>(v.value<int64_t>().value_or(0))));
			std::memcpy(dst, &value, sizeof(value));
			return;
		}
		case ShaderMemberType::bool_t: {
			const uint32_t value = v.value<bool>().value_or(false) ? 1u : 0u;
			std::memcpy(dst, &value, sizeof(value));
			return;
		}
		case ShaderMemberType::vec2: {
			std::array<float, 2> value {};
			readFloats(v, value.data(), value.size());
			std::memcpy(dst, value.data(), sizeof(value));
			return;
		}
		case ShaderMemberType::vec3: {
			std::array<float, 3> value {};
			readFloats(v, value.data(), value.size());
			std::memcpy(dst, value.data(), sizeof(value));
			return;
		}
		case ShaderMemberType::vec4: {
			std::array<float, 4> value {1.0f, 1.0f, 1.0f, 1.0f};
			readFloats(v, value.data(), value.size());
			std::memcpy(dst, value.data(), sizeof(value));
			return;
		}
		default: return;    // matrices and unknowns are engine-written or unsupported
	}
}

void writeMember(std::vector<std::byte>& blob, const ShaderBlockMember& member, const DataValue& v) {
	if (member.offset + member.size > blob.size()) {
		return;
	}

	if (member.element_count > 0) {
		if (!v.isArray()) {
			return;
		}
		const uint32_t stride = member.element_stride;
		const size_t count = std::min<size_t>(member.element_count, v.size());
		for (size_t i = 0; i < count; ++i) {
			writeScalar(blob, member.type, member.offset + (static_cast<uint32_t>(i) * stride), member.inspector, v[i]);
		}
		return;
	}

	writeScalar(blob, member.type, member.offset, member.inspector, v);
}

auto readTextureUID(const DataValue& v) -> toast::UID {
	// Texture parameters are objects with a "texture" key
	const DataValue* uid_value = &v;
	if (v.isObject() && v.contains("texture")) {
		uid_value = &v["texture"];
	}

	if (uid_value->type() == DataType::asset_t) {
		return static_cast<toast::UID>(*uid_value);
	}
	if (uid_value->type() == DataType::string_t) {
		const auto s = static_cast<std::string>(*uid_value);
		if (s.size() == 11) {
			return {toast::UID::fromString(s)};
		}
	}
	return toast::UID(uint64_t {0});
}

auto addressModeFromString(std::string_view str) -> vk::SamplerAddressMode {
	if (str == "mirrored") {
		return vk::SamplerAddressMode::eMirroredRepeat;
	}
	if (str == "clamp") {
		return vk::SamplerAddressMode::eClampToEdge;
	}
	if (str == "border") {
		return vk::SamplerAddressMode::eClampToBorder;
	}
	return vk::SamplerAddressMode::eRepeat;
}

auto filterFromString(std::string_view str) -> vk::Filter {
	return str == "nearest" ? vk::Filter::eNearest : vk::Filter::eLinear;
}

auto stringField(const DataValue& obj, std::string_view key, std::string_view fallback) -> std::string {
	if (obj.isObject() && obj.contains(key)) {
		if (auto s = obj[key].value<std::string>()) {
			return *s;
		}
	}
	return std::string(fallback);
}

}

MaterialRuntime::MaterialRuntime(const VulkanCore& core, assets::Material* material) : m_core(&core), m_material(material) {
	rebuild();
}

void MaterialRuntime::rebuild() {
	m_merged = {};
	m_entries.clear();
	m_values_dirty = true;
	m_textures_dirty = true;

	if (m_material == nullptr) {
		return;
	}

	// The pass always belongs to the root material
	assets::Material* root = m_material->rootMaterial();
	for (const auto& shader_handle : root->shaders()) {
		if (shader_handle.uid().data() == 0) {
			continue;
		}
		auto entry = ShaderCache::get().acquire(shader_handle.uid());
		if (!entry) {
			TOAST_WARN("Render", "Material '{}' references shader {} that failed to compile", root->name(), shader_handle.uid().get());
			continue;
		}

		// Merge reflection, first name wins
		for (const auto& binding : entry->reflection.bindings) {
			const bool exists = std::ranges::any_of(m_merged.bindings, [&](const auto& b) { return b.name == binding.name; });
			if (exists) {
				TOAST_WARN(
				    "Render", "Material '{}': shader parameter '{}' declared more than once, first wins", root->name(), binding.name
				);
				continue;
			}
			m_merged.bindings.push_back(binding);
		}
		for (const auto& push : entry->reflection.push_constants) {
			const bool exists = std::ranges::any_of(m_merged.push_constants, [&](const auto& p) { return p.name == push.name; });
			if (!exists) {
				m_merged.push_constants.push_back(push);
			}
		}
		for (const auto& name : entry->reflection.layout_order) {
			if (!std::ranges::contains(m_merged.layout_order, name)) {
				m_merged.layout_order.push_back(name);
			}
		}
		for (const auto& entry_point : entry->reflection.entry_points) {
			const bool exists = std::ranges::any_of(m_merged.entry_points, [&](const auto& e) { return e.name == entry_point.name; });
			if (!exists) {
				m_merged.entry_points.push_back(entry_point);
			}
		}

		m_entries.push_back(std::move(entry));
	}
}

auto MaterialRuntime::uniformBlobs() -> const std::vector<UboBlob>& {
	if (m_values_dirty) {
		bakeValues();
	}
	return m_ubo_blobs;
}

auto MaterialRuntime::pushBlob() -> const std::vector<std::byte>& {
	if (m_values_dirty) {
		bakeValues();
	}
	return m_push_blob;
}

void MaterialRuntime::bakeValues() {
	m_values_dirty = false;
	m_ubo_blobs.clear();
	m_push_blob.clear();
	m_model_offset.reset();

	if (m_material == nullptr) {
		return;
	}

	// Material-editable uniform buffers
	for (const auto& binding : m_merged.bindings) {
		if (binding.kind != ShaderBindingKind::uniform_buffer || !binding.engine_semantic.empty() || binding.size == 0) {
			continue;
		}

		UboBlob blob;
		blob.set = binding.set;
		blob.binding = binding.binding;
		blob.bytes.assign(binding.size, std::byte {0});

		for (const auto& member : binding.members) {
			// Only [Reflect] parameters are material data
			// Everything else is engine-owned
			if (!member.engine_semantic.empty() || !member.inspector.reflected) {
				continue;
			}
			if (const DataValue* v = resolveMemberValue(member)) {
				writeMember(blob.bytes, member, *v);
			}
		}
		m_ubo_blobs.push_back(std::move(blob));
	}

	// Push constants
	uint32_t push_size = 0;
	for (const auto& push : m_merged.push_constants) {
		push_size = std::max(push_size, push.size);
	}
	m_push_blob.assign(push_size, std::byte {0});

	for (const auto& push : m_merged.push_constants) {
		for (const auto& member : push.members) {
			if (member.engine_semantic == "model_matrix") {
				m_model_offset = member.offset;
				continue;
			}
			if (!member.engine_semantic.empty() || !member.inspector.reflected) {
				continue;
			}
			if (const DataValue* v = resolveMemberValue(member)) {
				writeMember(m_push_blob, member, *v);
			}
		}
	}
}

auto MaterialRuntime::groupTomlKey(const std::string& group) const -> std::string {
	for (const auto& binding : m_merged.bindings) {
		const bool is_texture =
		    binding.kind == ShaderBindingKind::combined_image_sampler || binding.kind == ShaderBindingKind::sampled_image;
		if (!is_texture) {
			continue;
		}
		if (binding.name == group || binding.inspector.display_name == group) {
			return binding.name;
		}
	}
	return group;
}

auto MaterialRuntime::resolveMemberValue(const ShaderBlockMember& member) const -> const DataValue* {
	if (m_material == nullptr) {
		return nullptr;
	}

	// Ungrouped parameters are top-level TOML keys
	if (member.inspector.group.empty()) {
		return m_material->value(member.name);
	}

	const DataValue* group = m_material->value(groupTomlKey(member.inspector.group));
	if (group == nullptr || !group->isObject()) {
		return nullptr;
	}

	const DataValue* scope = group;
	if (!member.inspector.subgroup.empty()) {
		if (!group->contains(member.inspector.subgroup)) {
			return nullptr;
		}
		scope = &(*group)[member.inspector.subgroup];
		if (!scope->isObject()) {
			return nullptr;
		}
	}

	return scope->contains(member.name) ? &(*scope)[member.name] : nullptr;
}

auto MaterialRuntime::textureSlots() -> const std::vector<TextureSlot>& {
	if (m_textures_dirty) {
		bakeTextures();
	}
	return m_texture_slots;
}

void MaterialRuntime::bakeTextures() {
	m_textures_dirty = false;
	m_texture_slots.clear();

	if (m_material == nullptr) {
		return;
	}

	for (const auto& binding : m_merged.bindings) {
		const bool is_texture =
		    binding.kind == ShaderBindingKind::combined_image_sampler || binding.kind == ShaderBindingKind::sampled_image;
		if (!is_texture || !binding.engine_semantic.empty()) {
			continue;
		}

		TextureSlot slot;
		slot.set = binding.set;
		slot.binding = binding.binding;

		const DataValue* v = m_material->value(binding.name);
		if (v != nullptr) {
			const toast::UID uid = readTextureUID(*v);
			if (uid.data() != 0) {
				slot.texture = assets::load<assets::Texture>(uid);
			}
			slot.sampler = samplerFor(v, binding.name);
		} else {
			slot.sampler = samplerFor(nullptr, binding.name);
		}

		m_texture_slots.push_back(std::move(slot));
	}
}

auto MaterialRuntime::samplerFor(const DataValue* params, std::string_view debug_name) -> vk::Sampler {
	const std::string repeat_u = params ? stringField(*params, "repeat_u", "repeat") : "repeat";
	const std::string repeat_v = params ? stringField(*params, "repeat_v", "repeat") : "repeat";
	const std::string min_filter = params ? stringField(*params, "min_filter", "linear") : "linear";
	const std::string mag_filter = params ? stringField(*params, "mag_filter", "linear") : "linear";
	const std::string mipmap_mode = params ? stringField(*params, "mipmap_mode", "linear") : "linear";
	bool anisotropy = true;
	if (params != nullptr && params->isObject() && params->contains("anisotropy")) {
		anisotropy = (*params)["anisotropy"].value<bool>().value_or(true);
	}

	// hash of the textual state
	const std::string state =
	    repeat_u + "|" + repeat_v + "|" + min_filter + "|" + mag_filter + "|" + mipmap_mode + "|" + (anisotropy ? "1" : "0");
	const uint64_t key = ShaderCache::fnv1a(state.data(), state.size());

	if (const auto it = m_samplers.find(key); it != m_samplers.end()) {
		return it->second->handle();
	}

	vk::SamplerCreateInfo info {};
	info.magFilter = filterFromString(mag_filter);
	info.minFilter = filterFromString(min_filter);
	info.addressModeU = addressModeFromString(repeat_u);
	info.addressModeV = addressModeFromString(repeat_v);
	info.addressModeW = vk::SamplerAddressMode::eRepeat;
	info.mipmapMode = mipmap_mode == "nearest" ? vk::SamplerMipmapMode::eNearest : vk::SamplerMipmapMode::eLinear;
	info.anisotropyEnable = (anisotropy && m_core->supportsSamplerAnisotropy()) ? vk::True : vk::False;
	info.maxAnisotropy = info.anisotropyEnable == vk::True ? std::min(16.0f, m_core->maxSamplerAnisotropy()) : 1.0f;
	info.compareEnable = vk::False;
	info.compareOp = vk::CompareOp::eAlways;
	info.unnormalizedCoordinates = vk::False;
	info.mipLodBias = 0.0f;
	info.minLod = 0.0f;
	info.maxLod = VK_LOD_CLAMP_NONE;

	auto sampler = std::make_unique<VulkanSampler>(
	    *m_core, info, std::format("MaterialRuntime Sampler ({} / {})", m_material ? m_material->name() : "?", debug_name)
	);
	const vk::Sampler handle = sampler->handle();
	m_samplers.emplace(key, std::move(sampler));
	return handle;
}

}
