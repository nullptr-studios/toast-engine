/// @file ShaderCompiler.hpp
/// @author dario
/// @date 17/05/2026.

#pragma once

#include "shader_reflection.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <toast/uid.hpp>
#include <vector>

namespace renderer {

struct CompiledShaderCode {
	std::vector<std::byte> spirv;
	ShaderReflection reflection;
	std::vector<std::string> dependencies;    // Virtual URIs the module depends on
};

class ShaderCompiler {
public:
	/**
	 * @brief Compiles a Slang module from in-memory source to SPIR-V
	 * @param uid UID of the shader asset
	 * @param source Slang source code
	 * @param source_uri Virtual URI of the source
	 */
	static auto compile(toast::UID uid, std::string_view source, std::string_view source_uri) -> CompiledShaderCode;
};

}
