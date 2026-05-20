/// @file ShaderCompiler.cpp
/// @author dario
/// @date 17/05/2026.

#include "ShaderCompiler.hpp"

#include "toast/log.hpp"

#include <cstring>
#include <fstream>
#include <iterator>
#include <slang-com-ptr.h>
#include <slang.h>
#include <stdexcept>

namespace toast::renderer {

static Slang::ComPtr<slang::IGlobalSession> slangGlobalSession;

static void ensureSlangGlobalSession() {
	if (slangGlobalSession) {
		return;
	}
	Slang::ComPtr<slang::IGlobalSession> session;
	SlangResult res = slang::createGlobalSession(session.writeRef());
	if (SLANG_FAILED(res)) {
		throw std::runtime_error("Failed to create Slang global session");
	}
	slangGlobalSession = session;
}

std::vector<std::byte> ShaderCompiler::compileShader(const std::filesystem::path& shaderPath) {
	std::filesystem::path resolvedPath = shaderPath;
	if (!resolvedPath.is_absolute()) {
		auto cwdPath = std::filesystem::current_path() / shaderPath;
		if (std::filesystem::exists(cwdPath)) {
			resolvedPath = cwdPath;
		} else {
			resolvedPath = shaderPath;
		}
	}

	TOAST_INFO("ShaderCompiler", "Attempting to compile shader from: {}", resolvedPath.string());
	TOAST_INFO("ShaderCompiler", "Current working directory: {}", std::filesystem::current_path().string());

	if (!std::filesystem::exists(resolvedPath)) {
		std::string errorMsg = "Shader file not found: " + resolvedPath.string() +
		                       "\nCurrent working directory: " + std::filesystem::current_path().string();
		throw std::runtime_error(errorMsg);
	}

	std::ifstream file(resolvedPath, std::ios::binary);
	if (!file) {
		throw std::runtime_error("Failed to open shader file: " + resolvedPath.string());
	}
	std::string source;
	source.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

	ensureSlangGlobalSession();

	// Prepare session desc to emit SPIR-V 1.4
	slang::TargetDesc target {};
	target.format = SLANG_SPIRV;
	target.profile = slangGlobalSession->findProfile("spirv_1_4");
	std::array<slang::TargetDesc, 1> slangTargets {target};

	slang::SessionDesc sessionDesc {};
	sessionDesc.targets = slangTargets.data();
	sessionDesc.targetCount = SlangInt(slangTargets.size());
	sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

	Slang::ComPtr<slang::ISession> slangSession;
	SlangResult r = slangGlobalSession->createSession(sessionDesc, slangSession.writeRef());
	if (SLANG_FAILED(r) || !slangSession) {
		throw std::runtime_error("Failed to create Slang compilation session");
	}

	const auto module_name = shaderPath.stem().string();
	Slang::ComPtr<slang::IModule> slangModule;
	Slang::ComPtr<slang::IBlob> slangDiagnostics;

	slangModule = slangSession->loadModuleFromSource(
	    module_name.c_str(), resolvedPath.string().c_str(), nullptr, slangDiagnostics.writeRef()
	);

	if (slangDiagnostics) {
		const void* diagPtr = slangDiagnostics->getBufferPointer();
		size_t diagSize = slangDiagnostics->getBufferSize();
		if (diagPtr && diagSize > 0) {
			const char* diagC = reinterpret_cast<const char*>(diagPtr);
			std::string diagStr(diagC, diagC + diagSize);
			TOAST_ERROR("ShaderCompiler", "Slang diagnostics (module load) for '{}':\n{}", resolvedPath.string(), diagStr);
		}
	}

	if (!slangModule) {
		throw std::runtime_error("Failed to load Slang module from source: " + resolvedPath.string());
	}

	Slang::ComPtr<slang::IBlob> spirvBlob;
	SlangResult got = slangModule->getTargetCode(0, spirvBlob.writeRef());
	if (SLANG_FAILED(got) || !spirvBlob) {
		throw std::runtime_error("Failed to get SPIR-V target code from Slang module");
	}

	const auto bufferSize = spirvBlob->getBufferSize();
	if (bufferSize == 0) {
		throw std::runtime_error("SPIR-V binary is empty");
	}

	const void* bufferPtr = spirvBlob->getBufferPointer();
	const auto* bytes = reinterpret_cast<const std::byte*>(bufferPtr);

	std::vector<std::byte> out;
	out.assign(bytes, bytes + bufferSize);

	TOAST_INFO("ShaderCompiler", "Successfully compiled shader from '{}' -> {} bytes of SPIR-V", resolvedPath.string(), bufferSize);

	return out;
}

}
