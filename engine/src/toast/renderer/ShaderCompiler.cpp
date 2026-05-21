/// @file ShaderCompiler.cpp
/// @author dario
/// @date 17/05/2026.

#include "ShaderCompiler.hpp"

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
	std::filesystem::path resolvedPath = shader_path;
	if (!resolvedPath.is_absolute()) {
		auto cwdPath = std::filesystem::current_path() / shader_path;
		if (std::filesystem::exists(cwdPath)) {
			resolvedPath = cwdPath;
		} else {
			resolvedPath = shader_path;
		}
	}

	TOAST_INFO("ShaderCompiler", "Attempting to compile shader from: {}", resolvedPath.string());
	TOAST_INFO("ShaderCompiler", "Current working directory: {}", std::filesystem::current_path().string());

	if (!std::filesystem::exists(resolvedPath)) {
		std::string errorMsg = "Shader file not found: " + resolvedPath.string() +
		                       "\nCurrent working directory: " + std::filesystem::current_path().string();
		TOAST_CRITICAL("ShaderCompiler", "{}", errorMsg);
	}

	std::ifstream file(resolvedPath, std::ios::binary);
	if (!file) {
		TOAST_CRITICAL("ShaderCompiler", "Failed to open shader file: {}", resolvedPath.string());
	}
	std::string source;
	source.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

	ensureSlangGlobalSession();

	slang::TargetDesc target {};
	target.format = SLANG_SPIRV;
	target.profile = slang_global_session->findProfile("spirv_1_6");
	std::array<slang::TargetDesc, 1> slang_targets {target};

	// FIX: Build explicit CompilerOptionEntries to enforce maximal optimization
	slang::CompilerOptionEntry opt_entry {};
	opt_entry.name = slang::CompilerOptionName::Optimization;
	opt_entry.value.kind = slang::CompilerOptionValueKind::Int;
	opt_entry.value.intValue0 = SlangOptimizationLevel::SLANG_OPTIMIZATION_LEVEL_MAXIMAL;

	std::array<slang::CompilerOptionEntry, 1> compiler_options {opt_entry};

	slang::SessionDesc session_desc {};
	session_desc.targets = slang_targets.data();
	session_desc.targetCount = SlangInt(slang_targets.size());
	session_desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
	// FIX: Assign options array to the session descriptor
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
	    module_name.c_str(), resolvedPath.string().c_str(), nullptr, slang_diagnostics.writeRef()
	);

	if (slang_diagnostics) {
		const void* diagPtr = slang_diagnostics->getBufferPointer();
		size_t diagSize = slang_diagnostics->getBufferSize();
		if (diagPtr && diagSize > 0) {
			const char* diagC = reinterpret_cast<const char*>(diagPtr);
			std::string diagStr(diagC, diagC + diagSize);
			TOAST_ERROR("ShaderCompiler", "Slang diagnostics (module load) for '{}':\n{}", resolvedPath.string(), diagStr);
		}
	}

	if (!slang_module) {
		TOAST_CRITICAL("ShaderCompiler", "Failed to load Slang module from source: {}", resolvedPath.string());
	}

	Slang::ComPtr<slang::IBlob> spirvBlob;
	SlangResult got = slang_module->getTargetCode(0, spirvBlob.writeRef());
	if (SLANG_FAILED(got) || !spirvBlob) {
		TOAST_CRITICAL("ShaderCompiler", "Failed to get SPIR-V target code from Slang module");
	}

	const auto bufferSize = spirvBlob->getBufferSize();
	if (bufferSize == 0) {
		TOAST_CRITICAL("ShaderCompiler", "SPIR-V binary is empty");
	}

	const void* bufferPtr = spirvBlob->getBufferPointer();
	const auto* bytes = reinterpret_cast<const std::byte*>(bufferPtr);

	std::vector<std::byte> out;
	out.assign(bytes, bytes + bufferSize);

	TOAST_INFO("ShaderCompiler", "Successfully compiled shader from '{}' -> {} bytes of SPIR-V", resolvedPath.string(), bufferSize);

	return out;
}

}
