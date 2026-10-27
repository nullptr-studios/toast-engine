/// @file ShaderCompiler.hpp
/// @author dario
/// @date 17/05/2026

#pragma once

#include "shader_reflection.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <toast/uid.hpp>
#include <vector>

namespace renderer {

struct CompiledShaderCode {
	std::vector<std::byte> spirv;
	ShaderReflection reflection;
	std::vector<std::string> dependencies;
};

class ShaderCompiler {
public:
	/// Thread safe
	static auto compile(toast::UID uid, std::string_view source, std::string_view source_uri) -> CompiledShaderCode;

	/// @note Call before any compilation
	static void setRayQueryAvailable(bool available);

	[[nodiscard]]
	static auto isRayQueryAvailable() -> bool;

	/// @note Call before any compilation
	static void setTraceRayQuery(bool enabled);

	/// Fixed at the first call until a restart
	[[nodiscard]]
	static auto traceRayQuery() -> bool;

	/// Player only
	/// @note Call before any compilation
	static void setFullOptimization(bool enabled);

	[[nodiscard]]
	static auto featureHash() -> uint64_t;
};

}
