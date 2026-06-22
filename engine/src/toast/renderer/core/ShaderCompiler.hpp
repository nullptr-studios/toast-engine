/// @file ShaderCompiler.hpp
/// @author dario
/// @date 17/05/2026.

#pragma once

#include <cstddef>
#include <filesystem>
#include <slang-com-ptr.h>
#include <slang.h>
#include <vector>

namespace toast::renderer {

/**
 * @struct CompiledShaderCode
 * @brief Represents the compiled shader code and reflection data
 */
struct CompiledShaderCode {
	std::vector<std::byte> spirv;
	Slang::ComPtr<slang::IComponentType> program;
};

/// @brief Compiles shader source files to SPIR-V using Slang
class ShaderCompiler {
public:
	static auto compileShaderModuleFromSource(const std::filesystem::path& shader_path) -> CompiledShaderCode;
	static auto compileShaderModule(std::string_view module_name) -> CompiledShaderCode;
};

}
