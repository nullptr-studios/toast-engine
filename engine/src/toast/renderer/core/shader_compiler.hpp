/// @file ShaderCompiler.hpp
/// @author dario
/// @date 17/05/2026.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <slang-com-ptr.h>
#include <slang.h>
#include <string>
#include <vector>

namespace toast::renderer {

struct ShaderDescriptorBinding {
	uint32_t set = 0;
	uint32_t binding = 0;
};

struct ShaderMaterialBindings {
	std::optional<ShaderDescriptorBinding> albedo_texture;
	std::optional<ShaderDescriptorBinding> albedo_sampler;

	[[nodiscard]]
	auto supportsAlbedoSampling() const -> bool {
		return albedo_texture.has_value() && albedo_sampler.has_value();
	}
};

struct CompiledShaderCode {
	std::vector<std::byte> spirv;
	Slang::ComPtr<slang::IComponentType> program;    // Holds the reflection data!
	ShaderMaterialBindings material_bindings;
};

class ShaderCompiler {
public:
	/// Compiles a GLSL shader file to SPIR-V
	static auto compileShaderModuleFromSource(const std::filesystem::path& shader_path) -> CompiledShaderCode;
	static auto compileShaderModule(std::string_view module_name) -> CompiledShaderCode;
};

}
