/// @file shader_compiler.cpp
/// @author dario
/// @date 17/05/2026.

#include "shader_compiler.hpp"

#include "toast/log.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <slang-com-ptr.h>
#include <slang.h>
#include <stdexcept>
#include <string_view>

namespace toast::renderer {

static Slang::ComPtr<slang::IGlobalSession> slang_global_session;
static Slang::ComPtr<slang::ISession> slang_session;

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
	std::array<const char*, 1> search_paths {R"(.\)"};                          // TODO: configure this correctly
	session_desc.searchPaths = search_paths.data();
	session_desc.searchPathCount = search_paths.size();

	// TODO Implement SLANGFilesystem for toastpkg

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
		TOAST_CRITICAL("ShaderCompiler", "Failed to create Slang compilation session");
	}
}

namespace {

auto toLower(std::string_view in) -> std::string {
	std::string out(in);
	std::transform(out.begin(), out.end(), out.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return out;
}

auto looksLikeAlbedo(std::string_view name) -> bool {
	const std::string lower = toLower(name);
	return lower.contains("albedo") || lower.contains("basecolor") || lower.contains("base_color");
}

auto isSampledTextureType(slang::TypeLayoutReflection* type_layout) -> bool {
	if (!type_layout || type_layout->getKind() != slang::TypeReflection::Kind::Resource) {
		return false;
	}

	const auto shape = type_layout->getResourceShape();
	const auto access = type_layout->getResourceAccess();
	const auto base_shape = shape & SLANG_RESOURCE_BASE_SHAPE_MASK;
	const bool is_texture = base_shape == SLANG_TEXTURE_1D || base_shape == SLANG_TEXTURE_2D || base_shape == SLANG_TEXTURE_3D ||
	                        base_shape == SLANG_TEXTURE_CUBE;
	return is_texture && access != SLANG_RESOURCE_ACCESS_READ_WRITE;
}

void reflectMaterialBindingsRecursive(
    slang::VariableLayoutReflection* var_layout, uint32_t current_space, uint32_t current_binding,
    ShaderMaterialBindings& out_bindings
) {
	if (!var_layout) {
		return;
	}

	const uint32_t binding_offset = var_layout->getOffset(slang::ParameterCategory::DescriptorTableSlot);
	const uint32_t space_offset = var_layout->getOffset(slang::ParameterCategory::SubElementRegisterSpace);

	const uint32_t next_space = current_space + space_offset;
	const uint32_t next_binding = current_binding + binding_offset;

	slang::TypeLayoutReflection* type_layout = var_layout->getTypeLayout();
	if (!type_layout) {
		return;
	}

	const auto type_kind = type_layout->getKind();
	const char* raw_name = var_layout->getName();
	const std::string var_name = raw_name ? raw_name : "";
	const bool albedo_name = looksLikeAlbedo(var_name);

	if (type_kind == slang::TypeReflection::Kind::ConstantBuffer || type_kind == slang::TypeReflection::Kind::ParameterBlock) {
		if (auto* element_layout = type_layout->getElementVarLayout()) {
			reflectMaterialBindingsRecursive(
			    element_layout, next_space, type_kind == slang::TypeReflection::Kind::ParameterBlock ? 0 : next_binding, out_bindings
			);
		}
		return;
	}

	if (type_kind == slang::TypeReflection::Kind::Struct) {
		const uint32_t field_count = type_layout->getFieldCount();
		for (uint32_t i = 0; i < field_count; ++i) {
			reflectMaterialBindingsRecursive(type_layout->getFieldByIndex(i), next_space, next_binding, out_bindings);
		}
		return;
	}

	if (albedo_name && var_layout->getCategory() == slang::ParameterCategory::DescriptorTableSlot) {
		if (type_kind == slang::TypeReflection::Kind::SamplerState) {
			out_bindings.albedo_sampler = ShaderDescriptorBinding {.set = next_space, .binding = next_binding};
			return;
		}
		if (isSampledTextureType(type_layout)) {
			out_bindings.albedo_texture = ShaderDescriptorBinding {.set = next_space, .binding = next_binding};
			return;
		}
	}
}

auto reflectMaterialBindings(slang::ProgramLayout* layout) -> ShaderMaterialBindings {
	ShaderMaterialBindings bindings;
	if (!layout) {
		return bindings;
	}

	if (auto* global_params = layout->getGlobalParamsVarLayout()) {
		reflectMaterialBindingsRecursive(global_params, 0, 0, bindings);
	}

	const uint32_t entry_point_count = layout->getEntryPointCount();
	for (uint32_t i = 0; i < entry_point_count; ++i) {
		if (auto* entry = layout->getEntryPointByIndex(i)) {
			if (auto* var_layout = entry->getVarLayout()) {
				reflectMaterialBindingsRecursive(var_layout, 0, 0, bindings);
			}
		}
	}

	return bindings;
}

}

auto ShaderCompiler::compileShaderModuleFromSource(const std::filesystem::path& shader_path) -> CompiledShaderCode {
	// TODO: SWAP WITH RESOURCE MANAGER!
	std::filesystem::path resolvedPath = shader_path;
	if (!resolvedPath.is_absolute()) {
		auto cwdPath = std::filesystem::current_path() / shader_path;
		if (std::filesystem::exists(cwdPath)) {
			resolvedPath = cwdPath;
		} else {
			resolvedPath = shader_path;
		}
	}

	TOAST_TRACE("ShaderCompiler", "Attempting to compile shader from: {}", resolvedPath.string());
	TOAST_TRACE("ShaderCompiler", "Current working directory: {}", std::filesystem::current_path().string());

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

	slang::IComponentType* components[] = {slang_module};
	Slang::ComPtr<slang::IComponentType> program;
	slang_session->createCompositeComponentType(components, 1, program.writeRef());

	slang::ProgramLayout* layout = program->getLayout();

	for (int i = 0; i < layout->getParameterCount(); i++) {
		auto r = layout->getParameterByIndex(i);
		TOAST_TRACE("ShaderCompiler", "variable name: {}\n  type: {}", r->getName(), r->getType()->getName());
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

	TOAST_TRACE(
	    "ShaderCompiler", "Successfully compiled shader from '{}' -> {} bytes of SPIR-V", resolvedPath.string(), bufferSize
	);

	CompiledShaderCode result;
	result.spirv = std::move(out);
	result.program = program;    // Save the composite component
	result.material_bindings = reflectMaterialBindings(layout);
	return result;
}

auto ShaderCompiler::compileShaderModule(std::string_view module_name) -> CompiledShaderCode {
	Slang::ComPtr<slang::IModule> slang_module;
	Slang::ComPtr<slang::IBlob> slang_diagnostics;

	slang_module = slang_session->loadModule(module_name.data(), slang_diagnostics.writeRef());

	if (slang_diagnostics) {
		const void* diagPtr = slang_diagnostics->getBufferPointer();
		size_t diagSize = slang_diagnostics->getBufferSize();
		if (diagPtr && diagSize > 0) {
			const char* diagC = reinterpret_cast<const char*>(diagPtr);
			std::string diagStr(diagC, diagC + diagSize);
			TOAST_ERROR("ShaderCompiler", "Slang diagnostics (module load) for '{}':\n{}", module_name, diagStr);
		}
	}

	slang_module->getDefinedEntryPointCount();

	slang::IComponentType* components[] = {slang_module};
	Slang::ComPtr<slang::IComponentType> program;
	slang_session->createCompositeComponentType(components, 1, program.writeRef());

	slang::ProgramLayout* layout = program->getLayout();

	for (int i = 0; i < layout->getParameterCount(); i++) {
		auto r = layout->getParameterByIndex(i);
		TOAST_TRACE("ShaderCompiler", "variable name: {}\n  type: {}", r->getName(), r->getType()->getName());
	}

	if (!slang_module) {
		TOAST_CRITICAL("ShaderCompiler", "Failed to load Slang module from module: {}", module_name);
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

	TOAST_TRACE("ShaderCompiler", "Successfully compiled shader from module '{}' -> {} bytes of SPIR-V", module_name, bufferSize);

	CompiledShaderCode result;
	result.spirv = std::move(out);
	result.program = program;    // Save the composite component
	result.material_bindings = reflectMaterialBindings(layout);
	return result;
}
}
