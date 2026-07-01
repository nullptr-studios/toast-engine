/// @file ShaderCompiler.cpp
/// @author dario
/// @date 17/05/2026.

#include "shader_compiler.hpp"

#include "toast/log.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <iterator>
#include <slang-com-ptr.h>
#include <slang.h>
#include <stdexcept>

namespace toast::renderer {

static Slang::ComPtr<slang::IGlobalSession> slang_global_session;

static void ensureSlangGlobalSession() {
	if (slang_global_session) {
		return;
	}
	Slang::ComPtr<slang::IGlobalSession> session;
	SlangResult res = slang::createGlobalSession(session.writeRef());
	if (SLANG_FAILED(res)) {
		TOAST_CRITICAL("ShaderCompiler", "Failed to create Slang global session");
	}
	slang_global_session = session;
}

auto ShaderCompiler::compileShader(const std::filesystem::path& shader_path) -> std::vector<std::byte> {
	// TODO: SWAP WITH RESOURCE MANAGER!
	std::filesystem::path resolved_path = shader_path;
	if (!resolved_path.is_absolute()) {
		auto cwd_path = std::filesystem::current_path() / shader_path;
		if (std::filesystem::exists(cwd_path)) {
			resolved_path = cwd_path;
		} else {
			resolved_path = shader_path;
		}
	}

	TOAST_TRACE("ShaderCompiler", "Attempting to compile shader from: {}", resolved_path.string());
	TOAST_TRACE("ShaderCompiler", "Current working directory: {}", std::filesystem::current_path().string());

	if (!std::filesystem::exists(resolved_path)) {
		std::string error_msg = "Shader file not found: " + resolved_path.string() +
		                        "\nCurrent working directory: " + std::filesystem::current_path().string();
		TOAST_CRITICAL("ShaderCompiler", "{}", error_msg);
	}

	std::ifstream file(resolved_path, std::ios::binary);
	if (!file) {
		TOAST_CRITICAL("ShaderCompiler", "Failed to open shader file: {}", resolved_path.string());
	}
	std::string source;
	source.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

	ensureSlangGlobalSession();

	// Set target to SPIR-V 1.6
	slang::TargetDesc target {};
	target.format = SLANG_SPIRV;
	target.profile = slang_global_session->findProfile("spirv_1_6");
	std::array<slang::TargetDesc, 1> slang_targets {target};

	// Dynamically configure options based on the build configuration
	std::vector<slang::CompilerOptionEntry> compiler_options;

#if !defined(NDEBUG)
	TOAST_INFO("ShaderCompiler", "Configuring Slang for DEBUG: Optimizations disabled, debug symbols enabled.");

	// Turn off optimizations entirely
	slang::CompilerOptionEntry opt_entry {};
	opt_entry.name = slang::CompilerOptionName::Optimization;
	opt_entry.value.kind = slang::CompilerOptionValueKind::Int;
	opt_entry.value.intValue0 = SlangOptimizationLevel::SLANG_OPTIMIZATION_LEVEL_NONE;
	compiler_options.push_back(opt_entry);

	// Embed full debug source
	slang::CompilerOptionEntry dbg_entry {};
	dbg_entry.name = slang::CompilerOptionName::DebugInformation;
	dbg_entry.value.kind = slang::CompilerOptionValueKind::Int;
	dbg_entry.value.intValue0 = SlangDebugInfoLevel::SLANG_DEBUG_INFO_LEVEL_MAXIMAL;
	compiler_options.push_back(dbg_entry);

#else
	TOAST_INFO("ShaderCompiler", "Configuring Slang for RELEASE: Maximum optimizations enabled.");

	// Enable maximal optimizations
	slang::CompilerOptionEntry opt_entry {};
	opt_entry.name = slang::CompilerOptionName::Optimization;
	opt_entry.value.kind = slang::CompilerOptionValueKind::Int;
	opt_entry.value.intValue0 = SlangOptimizationLevel::SLANG_OPTIMIZATION_LEVEL_MAXIMAL;
	compiler_options.push_back(opt_entry);

#endif

	slang::SessionDesc session_desc {};
	session_desc.targets = slang_targets.data();
	session_desc.targetCount = SlangInt(slang_targets.size());
	session_desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;    // glm
	session_desc.searchPathCount = 1;
	std::array<const char*, 1> search_paths {resolved_path.parent_path().string().c_str()};
	session_desc.searchPaths = search_paths.data();

	// TODO Implement SLANGFilesystem

#if !defined(NDEBUG)
	session_desc.skipSPIRVValidation = false;
#else
	session_desc.skipSPIRVValidation = true;
#endif

	// Bind our dynamic vector to the session descriptor
	session_desc.compilerOptionEntries = compiler_options.data();
	session_desc.compilerOptionEntryCount = SlangInt(compiler_options.size());

	Slang::ComPtr<slang::ISession> slang_session;
	SlangResult r = slang_global_session->createSession(session_desc, slang_session.writeRef());
	if (SLANG_FAILED(r) || !slang_session) {
		TOAST_CRITICAL("ShaderCompiler", "Failed to create Slang compilation session");
	}

	const auto module_name = shader_path.stem().string();
	Slang::ComPtr<slang::IModule> slang_module;
	Slang::ComPtr<slang::IBlob> slang_diagnostics;

	slang_module = slang_session->loadModuleFromSource(
	    module_name.c_str(), resolved_path.string().c_str(), nullptr, slang_diagnostics.writeRef()
	);

	if (slang_diagnostics) {
		const void* diag_ptr = slang_diagnostics->getBufferPointer();
		size_t diag_size = slang_diagnostics->getBufferSize();
		if (diag_ptr && diag_size > 0) {
			const char* diag_c = reinterpret_cast<const char*>(diag_ptr);
			std::string diag_str(diag_c, diag_c + diag_size);
			TOAST_ERROR("ShaderCompiler", "Slang diagnostics (module load) for '{}':\n{}", resolved_path.string(), diag_str);
		}
	}

	if (!slang_module) {
		TOAST_CRITICAL("ShaderCompiler", "Failed to load Slang module from source: {}", resolved_path.string());
	}

	Slang::ComPtr<slang::IBlob> spirv_blob;
	SlangResult got = slang_module->getTargetCode(0, spirv_blob.writeRef());
	if (SLANG_FAILED(got) || !spirv_blob) {
		TOAST_CRITICAL("ShaderCompiler", "Failed to get SPIR-V target code from Slang module");
	}

	const auto buffer_size = spirv_blob->getBufferSize();
	if (buffer_size == 0) {
		TOAST_CRITICAL("ShaderCompiler", "SPIR-V binary is empty");
	}

	const void* buffer_ptr = spirv_blob->getBufferPointer();
	const auto* bytes = reinterpret_cast<const std::byte*>(buffer_ptr);

	std::vector<std::byte> out;
	out.assign(bytes, bytes + buffer_size);

	TOAST_TRACE(
	    "ShaderCompiler", "Successfully compiled shader from '{}' -> {} bytes of SPIR-V", resolved_path.string(), buffer_size
	);

	return out;
}

}
