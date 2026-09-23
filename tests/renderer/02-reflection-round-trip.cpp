#include "test_registry.hpp"

#include <cassert>
#include <string>
#include <toast/renderer/shader_reflection.hpp>

// Every ShaderBindingKind has to survive toJson/fromJson unchanged
//
// This exists because acceleration_structure did not. ShaderReflection::toString() had no case for it, so it
// serialized as "uniform_buffer" and came back as a uniform buffer - which is not an error anywhere in the
// engine. The descriptor set layout was simply built with the wrong descriptor type, and the driver rejected
// the first write of the real type at run time, far from the omission that caused it
//
// The failure mode is what makes it worth a test: a fresh compile was always correct, so the fault only
// appeared on the *second* run of a build, after the poisoned entry reached the disk cache. Bumping
// ShaderCache's k_cache_format looked like a fix for exactly one launch. Any kind added to the enum without
// both string cases fails here instead
TOAST_TEST_NAMED("Renderer", "renderer/02-reflection-round-trip", test_renderer_02_reflection_round_trip) {
	using renderer::ShaderBindingKind;

	constexpr ShaderBindingKind kinds[] {
	  ShaderBindingKind::uniform_buffer,
	  ShaderBindingKind::storage_buffer,
	  ShaderBindingKind::combined_image_sampler,
	  ShaderBindingKind::sampled_image,
	  ShaderBindingKind::sampler,
	  ShaderBindingKind::storage_image,
	  ShaderBindingKind::acceleration_structure,
	};

	using renderer::ShaderMemberType;

	// Same reasoning for the block-member types: these decide the size and layout a material's uniform blob is
	// packed with, so one that round-trips to "unknown" writes the wrong bytes rather than failing
	constexpr ShaderMemberType member_types[] {
	  ShaderMemberType::bool_t,
	  ShaderMemberType::int_t,
	  ShaderMemberType::uint_t,
	  ShaderMemberType::float_t,
	  ShaderMemberType::vec2,
	  ShaderMemberType::vec3,
	  ShaderMemberType::vec4,
	  ShaderMemberType::mat3,
	  ShaderMemberType::mat4,
	};

	renderer::ShaderReflection reflection;
	uint32_t binding_index = 0;
	for (const auto kind : kinds) {
		renderer::ShaderBinding binding;
		binding.set = 0;
		binding.binding = binding_index;
		binding.name = "binding" + std::to_string(binding_index);
		binding.kind = kind;

		// Hung off the uniform buffer, which is the only kind that carries members
		if (kind == ShaderBindingKind::uniform_buffer) {
			uint32_t member_index = 0;
			for (const auto type : member_types) {
				renderer::ShaderBlockMember member;
				member.name = "member" + std::to_string(member_index);
				member.type = type;
				member.offset = member_index * 16;
				binding.members.push_back(std::move(member));
				++member_index;
			}
		}

		reflection.bindings.push_back(std::move(binding));
		++binding_index;
	}

	const auto restored = renderer::ShaderReflection::fromJson(reflection.toJson());
	assert(restored.has_value());
	assert(restored->bindings.size() == reflection.bindings.size());

	for (size_t i = 0; i < reflection.bindings.size(); ++i) {
		const auto& original = reflection.bindings[i];
		const auto& copy = restored->bindings[i];

		assert(copy.kind == original.kind);
		assert(copy.binding == original.binding);
		assert(copy.name == original.name);

		assert(copy.members.size() == original.members.size());
		for (size_t m = 0; m < original.members.size(); ++m) {
			assert(copy.members[m].type == original.members[m].type);
			assert(copy.members[m].name == original.members[m].name);
			assert(copy.members[m].offset == original.members[m].offset);
		}
	}
}
