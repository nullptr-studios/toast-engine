#include "test_registry.hpp"

#include <algorithm>
#include <cassert>
#include <slang-com-ptr.h>
#include <slang.h>
#include <string>
#include <vector>

namespace {

/// The imported module owns a set-0 binding, the importer owns a set-1 one. Inline rather than read off disk,
/// so this depends on Slang and nothing else - not the engine VFS, not the real shaders
constexpr const char* k_imported = R"(
struct Cam { float4x4 vp; float3 pos; float t; };

[[vk::binding(0,0)]]
ConstantBuffer<Cam> gCamera;

float3 shade(float3 n) { return gCamera.pos * n; }
)";

constexpr const char* k_importer = R"(
import imported_module;

[[vk::binding(0,1)]]
StructuredBuffer<uint> gOwn;

[shader("fragment")]
float4 fragmentMain(float3 n : TEXCOORD0) : SV_Target0
{
    return float4(shade(n) + float(gOwn[0]), 1.0);
}
)";

/// @brief Names every global parameter Slang's reflection API reports
///
/// @p link picks the layout: the raw composite, or the linked program. That difference is the whole subject
/// of this test, so it is a parameter rather than a fixed choice
auto reflectParameterNames(bool link) -> std::vector<std::string> {
	Slang::ComPtr<slang::IGlobalSession> global_session;
	slang::createGlobalSession(global_session.writeRef());
	assert(global_session);

	slang::TargetDesc target {};
	target.format = SLANG_SPIRV;
	target.profile = global_session->findProfile("spirv_1_5");

	slang::SessionDesc session_desc {};
	session_desc.targets = &target;
	session_desc.targetCount = 1;
	session_desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

	Slang::ComPtr<slang::ISession> session;
	global_session->createSession(session_desc, session.writeRef());
	assert(session);

	// Loaded first, so the import below resolves out of the session's module cache by name and no file system
	// is involved - the same way the engine's compiler resolves through its VFS
	Slang::ComPtr<slang::IBlob> diagnostics;
	slang::IModule* imported = session->loadModuleFromSourceString("imported_module", "imported_module.slang", k_imported, diagnostics.writeRef());
	assert(imported != nullptr);

	slang::IModule* importer = session->loadModuleFromSourceString("importer", "importer.slang", k_importer, diagnostics.writeRef());
	assert(importer != nullptr);

	slang::IComponentType* components[] = {importer};
	Slang::ComPtr<slang::IComponentType> program;
	session->createCompositeComponentType(components, 1, program.writeRef());
	assert(program);

	slang::ProgramLayout* layout = nullptr;
	Slang::ComPtr<slang::IComponentType> linked;
	if (link) {
		program->link(linked.writeRef(), diagnostics.writeRef());
		assert(linked);
		layout = linked->getLayout();
	} else {
		layout = program->getLayout();
	}
	assert(layout != nullptr);

	std::vector<std::string> names;
	for (uint32_t i = 0; i < layout->getParameterCount(); ++i) {
		if (auto* parameter = layout->getParameterByIndex(i); parameter != nullptr && parameter->getName() != nullptr) {
			names.emplace_back(parameter->getName());
		}
	}
	return names;
}

auto contains(const std::vector<std::string>& names, std::string_view wanted) -> bool {
	return std::ranges::find(names, wanted) != names.end();
}

}

/// ShaderReflection drives the pipeline layout. When a parameter an *imported* module declares is missing
/// from it, the descriptor is still compiled into the SPIR-V but absent from the layout - the shader then
/// reads an undeclared binding, which renders the scene black behind a single validation message
///
/// lighting.slang owns the whole of descriptor set 0, so this is the difference between every lit shader in
/// the engine working and none of them working. Pinned here because it is a property of the Slang API rather
/// than of our code, and nothing else would notice it changing
TOAST_TEST_NAMED("renderer", "renderer/04-import-reflection", test_renderer_04_import_reflection) {
	const auto linked = reflectParameterNames(true);

	// The importer's own parameter, which was never in doubt
	assert(contains(linked, "gOwn"));

	// The one that matters: declared by the imported module, read through a function call
	//
	// Measured, not assumed: reflectParameterNames(false) - the unlinked composite the compiler used to
	// reflect - reports gOwn and *not* gCamera. Not asserted here because a future Slang could reasonably
	// close that gap, and this test should keep passing when it does
	assert(contains(linked, "gCamera"));
}
