/// @file ShaderCompiler.hpp
/// @author dario
/// @date 17/05/2026.

#pragma once

#include <cstddef>
#include <filesystem>
#include <slang-com-ptr.h>
#include <slang.h>
#include <string>
#include <string_view>
#include <toast/uid.hpp>
#include <vector>

namespace renderer {

struct CompiledShaderCode {
	std::vector<std::byte> spirv;
	Slang::ComPtr<slang::IComponentType> program;    // Holds the reflection data!
	std::vector<std::string> dependencies;           // Virtual URIs the module depends on
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

	[[deprecated("You should be using the asset manager for loading shaders")]]
	static auto compileShaderModuleFromSource(const std::filesystem::path& shader_path) -> CompiledShaderCode;
};

}
