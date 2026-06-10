/// @file ShaderCompiler.hpp
/// @author dario
/// @date 17/05/2026.

#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

namespace toast::renderer {

class ShaderCompiler {
public:
	/// Compiles a GLSL shader file to SPIR-V
	static auto compileShader(const std::filesystem::path& shader_path) -> std::vector<std::byte>;
};

}
