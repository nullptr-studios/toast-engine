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
	static std::vector<std::byte> compileShader(const std::filesystem::path& shaderPath);
};

}
