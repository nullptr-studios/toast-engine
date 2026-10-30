/// @file shader_compiler.cpp
/// @author dario
/// @date 17/05/2026

#include "shader_compiler.hpp"

#include "depth_convention.hpp"
#include "slang_vfs.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <slang-com-ptr.h>
#include <slang.h>
#include <thread>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>
#include <vector>

namespace renderer {

namespace {

/// Global sessions are not thread safe so compiles lease one and the cap bounds core modules in memory
class SlangGlobalSessionPool {
public:
	class Lease {
	public:
		Lease() : m_session(SlangGlobalSessionPool::get().acquire()) { }

		~Lease() { SlangGlobalSessionPool::get().release(std::move(m_session)); }

		Lease(const Lease&) = delete;
		auto operator=(const Lease&) -> Lease& = delete;
		Lease(Lease&&) = delete;
		auto operator=(Lease&&) -> Lease& = delete;

		[[nodiscard]]
		auto get() const -> slang::IGlobalSession* {
			return m_session.get();
		}

	private:
		Slang::ComPtr<slang::IGlobalSession> m_session;
	};

	static auto get() -> SlangGlobalSessionPool& {
		static SlangGlobalSessionPool pool;
		return pool;
	}

	auto acquire() -> Slang::ComPtr<slang::IGlobalSession> {
		{
			std::unique_lock lock(m_mutex);
			m_available.wait(lock, [this] { return !m_free.empty() || m_created < m_limit; });
			if (!m_free.empty()) {
				auto session = std::move(m_free.back());
				m_free.pop_back();
				return session;
			}
			++m_created;
		}

		ZoneScopedN("Create Slang global session");
		Slang::ComPtr<slang::IGlobalSession> session;
		if (SLANG_FAILED(slang::createGlobalSession(session.writeRef())) || !session) {
			TOAST_CRITICAL("Render", "Failed to create Slang global session");
		}
		return session;
	}

	void release(Slang::ComPtr<slang::IGlobalSession> session) {
		{
			const std::lock_guard lock(m_mutex);
			m_free.push_back(std::move(session));
		}
		m_available.notify_one();
	}

	void setLimit(size_t limit) {
		{
			const std::lock_guard lock(m_mutex);
			m_limit = std::max<size_t>(limit, 1);
		}
		m_available.notify_all();
	}

private:
	SlangGlobalSessionPool() : m_limit(std::clamp<size_t>(std::thread::hardware_concurrency() / 2, 1, 4)) { }

	std::mutex m_mutex;
	std::condition_variable m_available;
	std::vector<Slang::ComPtr<slang::IGlobalSession>> m_free;
	size_t m_created = 0;
	size_t m_limit;
};

std::atomic<RayTracingMode> ray_tracing_mode = RayTracingMode::automatic;
std::atomic_bool ray_query_available = false;
std::atomic_bool full_optimization = false;
std::atomic_bool trace_ray_query_requested = true;

}

void ShaderCompiler::setRayTracingMode(RayTracingMode mode) {
	ray_tracing_mode.store(mode, std::memory_order_relaxed);
}

auto ShaderCompiler::rayTracingMode() -> RayTracingMode {
	return ray_tracing_mode.load(std::memory_order_relaxed);
}

void ShaderCompiler::setRayQueryAvailable(bool available) {
	ray_query_available.store(available, std::memory_order_relaxed);
}

auto ShaderCompiler::isRayQueryAvailable() -> bool {
	return ray_query_available.load(std::memory_order_relaxed);
}

void ShaderCompiler::setTraceRayQuery(bool enabled) {
	trace_ray_query_requested.store(enabled, std::memory_order_relaxed);
}

auto ShaderCompiler::traceRayQuery() -> bool {
	static const bool fixed = isRayQueryAvailable() && trace_ray_query_requested.load(std::memory_order_relaxed);
	return fixed;
}

void ShaderCompiler::setFullOptimization(bool enabled) {
	full_optimization.store(enabled, std::memory_order_relaxed);
}

void ShaderCompiler::setMaxParallelCompiles(size_t count) {
	SlangGlobalSessionPool::get().setLimit(count);
}

auto ShaderCompiler::featureHash() -> uint64_t {
	// Debug compiles unoptimized with debug info so a release entry in the shared cache must read as stale and the reverse
#if defined(NDEBUG)
	const uint64_t k_build_profile = full_optimization.load(std::memory_order_relaxed) ? 0x0ull : 0x0f7d1e5ca1ab1e00ull;
#else
	constexpr uint64_t k_build_profile = 0xd3b09c0de5a11e01ull;
#endif
	// TOAST_REVERSED_Z changes the SPIR-V so the other convention must read as stale
	constexpr uint64_t k_depth_convention = depth::k_reversed_z ? 0x5eed2e7e45edull : 0x0ull;
	const uint64_t k_trace_walk = traceRayQuery() ? 0x0ull : 0x7a11c0de5eedull;
#if defined(TOAST_DEV_TOOLS)
	constexpr uint64_t k_dev_tools = 0xde7700150000ull;
#else
	constexpr uint64_t k_dev_tools = 0x0ull;
#endif
	return (ShaderCompiler::isRayQueryAvailable() ? 0x9e3779b97f4a7c15ull : 0x0ull) ^ k_build_profile ^ k_depth_convention ^
	       k_trace_walk ^ k_dev_tools;
}

static auto createSession(slang::IGlobalSession* global_session) -> Slang::ComPtr<slang::ISession> {
	ZoneScoped;

	slang::TargetDesc target {};
	target.format = SLANG_SPIRV;
	target.profile = global_session->findProfile("spirv_1_6");
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
	const bool full = full_optimization.load(std::memory_order_relaxed);
	TOAST_TRACE("Render", "Configuring Slang for RELEASE with {} optimization", full ? "maximal" : "default");

	slang::CompilerOptionEntry opt_entry {};
	opt_entry.name = slang::CompilerOptionName::Optimization;
	opt_entry.value.kind = slang::CompilerOptionValueKind::Int;
	opt_entry.value.intValue0 =
	    full ? SlangOptimizationLevel::SLANG_OPTIMIZATION_LEVEL_MAXIMAL : SlangOptimizationLevel::SLANG_OPTIMIZATION_LEVEL_DEFAULT;
	compiler_options.push_back(opt_entry);

#endif

	// Always defined to 0 or 1 so shaders can #if TOAST_RAY_QUERY
	slang::CompilerOptionEntry ray_query_entry {};
	ray_query_entry.name = slang::CompilerOptionName::MacroDefine;
	ray_query_entry.value.kind = slang::CompilerOptionValueKind::String;
	ray_query_entry.value.stringValue0 = "TOAST_RAY_QUERY";
	ray_query_entry.value.stringValue1 = ShaderCompiler::isRayQueryAvailable() ? "1" : "0";
	compiler_options.push_back(ray_query_entry);

	slang::CompilerOptionEntry trace_entry {};
	trace_entry.name = slang::CompilerOptionName::MacroDefine;
	trace_entry.value.kind = slang::CompilerOptionValueKind::String;
	trace_entry.value.stringValue0 = "TOAST_TRACE_RAY_QUERY";
	trace_entry.value.stringValue1 = ShaderCompiler::traceRayQuery() ? "1" : "0";
	compiler_options.push_back(trace_entry);

	// Always defined to 0 or 1
	slang::CompilerOptionEntry dev_tools_entry {};
	dev_tools_entry.name = slang::CompilerOptionName::MacroDefine;
	dev_tools_entry.value.kind = slang::CompilerOptionValueKind::String;
	dev_tools_entry.value.stringValue0 = "TOAST_DEV_TOOLS";
#if defined(TOAST_DEV_TOOLS)
	dev_tools_entry.value.stringValue1 = "1";
#else
	dev_tools_entry.value.stringValue1 = "0";
#endif
	compiler_options.push_back(dev_tools_entry);

	// Always defined to 0 or 1
	slang::CompilerOptionEntry reversed_z_entry {};
	reversed_z_entry.name = slang::CompilerOptionName::MacroDefine;
	reversed_z_entry.value.kind = slang::CompilerOptionValueKind::String;
	reversed_z_entry.value.stringValue0 = "TOAST_REVERSED_Z";
	reversed_z_entry.value.stringValue1 = depth::k_reversed_z ? "1" : "0";
	compiler_options.push_back(reversed_z_entry);

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
	SlangResult r = global_session->createSession(session_desc, session.writeRef());
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
		const char* diag_c = static_cast<const char*>(diag_ptr);
		const std::string diag_str(diag_c, diag_c + diag_size);

		// Default to warning
		logging::Severity severity = logging::Severity::warning;

		if (diag_str.contains(": error ")) {
			severity = logging::Severity::error;
		}

		TOAST_MESSAGE(severity, "Render", std::format("Slang diagnostics for '{}'", source_name), diag_str);
	}
}

auto compileModule(std::string_view module_name, std::string_view source_path, std::string_view source) -> CompiledShaderCode {
	ZoneScoped;
	const SlangGlobalSessionPool::Lease global_session;
	auto slang_session = createSession(global_session.get());

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

	const auto* bytes = static_cast<const std::byte*>(spirv_blob->getBufferPointer());

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
