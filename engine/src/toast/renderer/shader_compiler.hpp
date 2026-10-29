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

/// Mirrors the options of renderer.quality.ray_tracing
enum class RayTracingMode : uint8_t {
	automatic,
	on,
	off,
};

class ShaderCompiler {
public:
	/// @note Call before the Vulkan device exists
	static void setRayTracingMode(RayTracingMode mode);

	[[nodiscard]]
	static auto rayTracingMode() -> RayTracingMode;

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

	/// Each compile holds a Slang session of up to 1 GB
	static void setMaxParallelCompiles(size_t count);

	[[nodiscard]]
	static auto featureHash() -> uint64_t;
};

}
