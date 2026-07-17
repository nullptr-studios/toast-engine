/// @file shader_compiler.cpp
/// @author dario
/// @date 17/05/2026.

#include "shader_compiler.hpp"

#include "slang_vfs.hpp"

#include <array>
#include <slang-com-ptr.h>
#include <slang.h>
#include <toast/log.hpp>

namespace renderer {

static Slang::ComPtr<slang::IGlobalSession> slang_global_session;
static Slang::ComPtr<slang::ISession> slang_session;

static void ensureSlangGlobalSession() {
	if (slang_global_session) {
		return;
	}
	Slang::ComPtr<slang::IGlobalSession> session;
	SlangResult res = slang::createGlobalSession(session.writeRef());
	if (SLANG_FAILED(res)) {
		TOAST_CRITICAL("Render", "Failed to create Slang global session");
	}
	slang_global_session = session;

	// Set target to SPIR-V 1.6
	slang::TargetDesc target {};
	target.format = SLANG_SPIRV;
	target.profile = slang_global_session->findProfile("spirv_1_6");
	std::array<slang::TargetDesc, 1> slang_targets {target};

	// Dynamically configure options based on the build configuration
	std::vector<slang::CompilerOptionEntry> compiler_options;

#if !defined(NDEBUG)
	TOAST_INFO("Render", "Configuring Slang for DEBUG: Optimizations disabled, debug symbols enabled.");

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
	TOAST_INFO("Render", "Configuring Slang for RELEASE: Maximum optimizations enabled.");

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

	// Imports resolve through the engine VFS
	session_desc.fileSystem = &SlangVfs::get();
	std::array<const char*, 1> search_paths {"core://shaders/"};
	session_desc.searchPaths = search_paths.data();
	session_desc.searchPathCount = search_paths.size();

#if !defined(NDEBUG)
	session_desc.skipSPIRVValidation = false;
#else
	session_desc.skipSPIRVValidation = true;
#endif

	// Bind our dynamic vector to the session descriptor
	session_desc.compilerOptionEntries = compiler_options.data();
	session_desc.compilerOptionEntryCount = SlangInt(compiler_options.size());

	SlangResult r = slang_global_session->createSession(session_desc, slang_session.writeRef());
	if (SLANG_FAILED(r) || !slang_session) {
		TOAST_CRITICAL("Render", "Failed to create Slang compilation session");
	}
}

namespace {

void logDiagnostics(const Slang::ComPtr<slang::IBlob>& diagnostics, std::string_view source_name) {
	if (!diagnostics) {
		return;
	}
	const void* diag_ptr = diagnostics->getBufferPointer();
	const size_t diag_size = diagnostics->getBufferSize();
	if (diag_ptr && diag_size > 0) {
		const char* diag_c = reinterpret_cast<const char*>(diag_ptr);
		const std::string diag_str(diag_c, diag_c + diag_size);
		TOAST_ERROR("Render", "Slang diagnostics for '{}':\n{}", source_name, diag_str);
	}
}

auto compileModule(std::string_view module_name, std::string_view source_path, std::string_view source) -> CompiledShaderCode {
	ensureSlangGlobalSession();

	Slang::ComPtr<slang::IModule> slang_module;
	Slang::ComPtr<slang::IBlob> slang_diagnostics;

	Slang::ComPtr<ISlangBlob> source_blob;
	source_blob.attach(SlangVfs::makeBlob(source.data(), source.size()));

	const std::string module_name_str(module_name);
	const std::string source_path_str(source_path);
	slang_module = slang_session->loadModuleFromSource(
	    module_name_str.c_str(), source_path_str.c_str(), source_blob, slang_diagnostics.writeRef()
	);

	logDiagnostics(slang_diagnostics, source_path);

	if (!slang_module) {
		TOAST_ERROR("Render", "Failed to load Slang module: {}", source_path);
		return {};
	}

	const std::array<slang::IComponentType*, 1> components {slang_module};
	Slang::ComPtr<slang::IComponentType> program;
	slang_session->createCompositeComponentType(components.data(), 1, program.writeRef());
	if (!program) {
		TOAST_ERROR("Render", "Failed to create Slang composite component for: {}", source_path);
		return {};
	}

	Slang::ComPtr<slang::IBlob> spirv_blob;
	Slang::ComPtr<slang::IBlob> spirv_diagnostics;
	const SlangResult got = slang_module->getTargetCode(0, spirv_blob.writeRef(), spirv_diagnostics.writeRef());
	logDiagnostics(spirv_diagnostics, source_path);
	if (SLANG_FAILED(got) || !spirv_blob || spirv_blob->getBufferSize() == 0) {
		TOAST_ERROR("Render", "Failed to get SPIR-V target code for: {}", source_path);
		return {};
	}

	const auto* bytes = reinterpret_cast<const std::byte*>(spirv_blob->getBufferPointer());

	CompiledShaderCode result;
	result.spirv.assign(bytes, bytes + spirv_blob->getBufferSize());
	result.reflection = extractReflection(program->getLayout());

	// Collect dependencies as virtual URIs
	const SlangInt32 dependency_count = slang_module->getDependencyFileCount();
	for (SlangInt32 i = 0; i < dependency_count; ++i) {
		const char* dep_path = slang_module->getDependencyFilePath(i);
		if (dep_path == nullptr) {
			continue;
		}
		auto uri = SlangVfs::normalizeUri(dep_path);
		if (!uri.empty() && uri != source_path) {
			result.dependencies.push_back(std::move(uri));
		}
	}

	// we dont want to log this two times
	// TOAST_TRACE("Render", "Compiled shader '{}' -> {} bytes of SPIR-V", source_path, spirv_blob->getBufferSize());
	return result;
}

}

auto ShaderCompiler::compile(toast::UID uid, std::string_view source, std::string_view source_uri) -> CompiledShaderCode {
	return compileModule(uid.get(), source_uri, source);
}

}
