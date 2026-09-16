/// @file shader_compiler.cpp
/// @author dario
/// @date 17/05/2026

#include "shader_compiler.hpp"

#include "slang_vfs.hpp"

#include <algorithm>
#include <array>
#include <slang-com-ptr.h>
#include <slang.h>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

static Slang::ComPtr<slang::IGlobalSession> slang_global_session;

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
}

static bool ray_query_available = false;

void ShaderCompiler::setRayQueryAvailable(bool available) {
	ray_query_available = available;
}

auto ShaderCompiler::isRayQueryAvailable() -> bool {
	return ray_query_available;
}

auto ShaderCompiler::featureHash() -> uint64_t {
	return ray_query_available ? 0x9e3779b97f4a7c15ull : 0x0ull;
}

static auto createSession() -> Slang::ComPtr<slang::ISession> {
	ZoneScoped;
	ensureSlangGlobalSession();

	slang::TargetDesc target {};
	target.format = SLANG_SPIRV;
	target.profile = slang_global_session->findProfile("spirv_1_6");
	std::array<slang::TargetDesc, 1> slang_targets {target};

	std::vector<slang::CompilerOptionEntry> compiler_options;

#if !defined(NDEBUG)
	TOAST_INFO("Render", "Configuring Slang for DEBUG: Optimizations disabled, debug symbols enabled.");

	slang::CompilerOptionEntry opt_entry {};
	opt_entry.name = slang::CompilerOptionName::Optimization;
	opt_entry.value.kind = slang::CompilerOptionValueKind::Int;
	opt_entry.value.intValue0 = SlangOptimizationLevel::SLANG_OPTIMIZATION_LEVEL_NONE;
	compiler_options.push_back(opt_entry);

	slang::CompilerOptionEntry dbg_entry {};
	dbg_entry.name = slang::CompilerOptionName::DebugInformation;
	dbg_entry.value.kind = slang::CompilerOptionValueKind::Int;
	dbg_entry.value.intValue0 = SlangDebugInfoLevel::SLANG_DEBUG_INFO_LEVEL_MAXIMAL;
	compiler_options.push_back(dbg_entry);

#else
	TOAST_INFO("Render", "Configuring Slang for RELEASE: Maximum optimizations enabled.");

	slang::CompilerOptionEntry opt_entry {};
	opt_entry.name = slang::CompilerOptionName::Optimization;
	opt_entry.value.kind = slang::CompilerOptionValueKind::Int;
	opt_entry.value.intValue0 = SlangOptimizationLevel::SLANG_OPTIMIZATION_LEVEL_MAXIMAL;
	compiler_options.push_back(opt_entry);

#endif

	// Always defined to 0 or 1 so shaders can #if TOAST_RAY_QUERY
	slang::CompilerOptionEntry ray_query_entry {};
	ray_query_entry.name = slang::CompilerOptionName::MacroDefine;
	ray_query_entry.value.kind = slang::CompilerOptionValueKind::String;
	ray_query_entry.value.stringValue0 = "TOAST_RAY_QUERY";
	ray_query_entry.value.stringValue1 = ray_query_available ? "1" : "0";
	compiler_options.push_back(ray_query_entry);

	slang::SessionDesc session_desc {};
	session_desc.targets = slang_targets.data();
	session_desc.targetCount = SlangInt(slang_targets.size());
	session_desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;    // glm

	session_desc.fileSystem = &SlangVfs::get();
	std::array<const char*, 1> search_paths {"core://shaders/"};
	session_desc.searchPaths = search_paths.data();
	session_desc.searchPathCount = search_paths.size();

#if !defined(NDEBUG)
	session_desc.skipSPIRVValidation = false;
#else
	session_desc.skipSPIRVValidation = true;
#endif

	session_desc.compilerOptionEntries = compiler_options.data();
	session_desc.compilerOptionEntryCount = SlangInt(compiler_options.size());

	Slang::ComPtr<slang::ISession> session;
	SlangResult r = slang_global_session->createSession(session_desc, session.writeRef());
	if (SLANG_FAILED(r) || !session) {
		TOAST_CRITICAL("Render", "Failed to create Slang compilation session");
	}
	return session;
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
	ZoneScoped;
	auto slang_session = createSession();

	Slang::ComPtr<slang::IModule> slang_module;
	Slang::ComPtr<slang::IBlob> slang_diagnostics;

	Slang::ComPtr<ISlangBlob> source_blob;
	source_blob.attach(SlangVfs::makeBlob(source.data(), source.size()));

	const std::string module_name_str(module_name);
	const std::string source_path_str(source_path);

	SlangVfs::Recorder import_recorder;
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

	// Reflection must come from the linked program since the composite misses parameters owned by imports
	Slang::ComPtr<slang::IComponentType> linked_program;
	Slang::ComPtr<slang::IBlob> link_diagnostics;
	program->link(linked_program.writeRef(), link_diagnostics.writeRef());
	logDiagnostics(link_diagnostics, source_path);
	if (!linked_program) {
		TOAST_ERROR("Render", "Failed to link Slang program for: {}", source_path);
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
	result.reflection = extractReflection(linked_program->getLayout());
	extractModuleEntryPoints(slang_module, result.reflection);

	auto add_dependency = [&](std::string uri) {
		if (uri.empty() || uri == source_path || std::ranges::contains(result.dependencies, uri)) {
			return;
		}
		result.dependencies.push_back(std::move(uri));
	};

	for (const auto& uri : import_recorder.resolved()) {
		add_dependency(uri);
	}

	const SlangInt32 dependency_count = slang_module->getDependencyFileCount();
	for (SlangInt32 i = 0; i < dependency_count; ++i) {
		if (const char* dep_path = slang_module->getDependencyFilePath(i); dep_path != nullptr) {
			add_dependency(SlangVfs::normalizeUri(dep_path));
		}
	}

	return result;
}

}

auto ShaderCompiler::compile(toast::UID uid, std::string_view source, std::string_view source_uri) -> CompiledShaderCode {
	return compileModule(uid.get(), source_uri, source);
}

}
