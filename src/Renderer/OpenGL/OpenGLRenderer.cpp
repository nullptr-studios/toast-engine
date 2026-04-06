//
// Created by dario on 14/09/2025.
//

#include "Toast/Renderer/OpenGL/OpenGLRenderer.hpp"

#include "Toast/GlmJson.hpp"
#include "Toast/Log.hpp"
#include "Toast/Profiler.hpp"
#include "Toast/Renderer/HUD/HUDLayer.hpp"
#include "Toast/Renderer/LayerStack.hpp"
#include "Toast/Renderer/Shader.hpp"
#include "Toast/Resources/Mesh.hpp"
#include "Toast/Resources/ResourceManager.hpp"
#include "Toast/Window/Window.hpp"
#include "Toast/Window/WindowEvents.hpp"

#include <SDL3/SDL.h>
#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>

#ifdef TOAST_EDITOR
// clang-format off
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_opengl3.h>
#include <ImGuizmo.h>
// clang-format on
#endif

#include "Toast/Renderer/IRenderable.hpp"
#include "Toast/Renderer/OclussionVolume.hpp"
#include "Toast/Renderer/PostProcessing/ColorGrading.hpp"
#include "Toast/Renderer/PostProcessing/Tonemaping.hpp"

#include <algorithm>
#include <sstream>
#include <stb_image.h>

#ifdef TRACY_ENABLE
#include <tracy/TracyOpenGL.hpp>
#endif

namespace renderer {

IRendererBase* IRendererBase::m_instance = nullptr;

#ifdef _DEBUG
#define CHECK_GL()                                     \
	{                                                    \
		if (GLenum err = glGetError()) {                   \
			TOAST_ERROR("GL Error: {}", glErrorString(err)); \
		}                                                  \
	}
#else
#define CHECK_GL()
#endif

inline const char* glErrorString(GLenum err) noexcept {
	switch (err) {
		case GL_NO_ERROR: return "GL_NO_ERROR";
		case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
		case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
		case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
		case GL_STACK_OVERFLOW: return "GL_STACK_OVERFLOW";
		case GL_STACK_UNDERFLOW: return "GL_STACK_UNDERFLOW";
		case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
		case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
		default: return "UNKNOWN ERROR";
	}
}

#ifdef TOAST_EDITOR
static SDL_Window* g_backup_current_window = nullptr;
static SDL_GLContext g_backup_current_context = nullptr;
#endif

#ifndef NDEBUG
void DebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* user_param) {
	// ignore non-significant error/warning codes
	if (id == 131169 || id == 131185 || id == 131218 || id == 131204 || id == 1) {
		return;
	}

	std::stringstream ss = {};
	ss << "================================================================================\n";
	ss << "                         OPENGL DEBUG MESSAGE\n";
	ss << "================================================================================\n";
	ss << "Message ID: " << id << "\n";
	ss << "Message: " << message << "\n";
	ss << "Length: " << length << " characters\n\n";

	// Source
	ss << "--- SOURCE ---\n";
	switch (source) {
		case GL_DEBUG_SOURCE_API: ss << "API (OpenGL function calls)\n"; break;
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM: ss << "Window System (platform-specific)\n"; break;
		case GL_DEBUG_SOURCE_SHADER_COMPILER: ss << "Shader Compiler (shader compilation)\n"; break;
		case GL_DEBUG_SOURCE_THIRD_PARTY: ss << "Third Party (external library)\n"; break;
		case GL_DEBUG_SOURCE_APPLICATION: ss << "Application (your code)\n"; break;
		case GL_DEBUG_SOURCE_OTHER: ss << "Other (unknown source)\n"; break;
		default: ss << "Unknown source: " << std::hex << source << std::dec << "\n"; break;
	}

	// Type
	ss << "\n--- TYPE ---\n";
	switch (type) {
		case GL_DEBUG_TYPE_ERROR: ss << "ERROR - OpenGL error\n"; break;
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: ss << "DEPRECATED BEHAVIOR - Use of deprecated functionality\n"; break;
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: ss << "UNDEFINED BEHAVIOR - Undefined behavior in OpenGL\n"; break;
		case GL_DEBUG_TYPE_PORTABILITY: ss << "PORTABILITY - Non-portable code\n"; break;
		case GL_DEBUG_TYPE_PERFORMANCE: ss << "PERFORMANCE - Performance-impacting code\n"; break;
		case GL_DEBUG_TYPE_MARKER: ss << "MARKER - User-inserted debug marker\n"; break;
		case GL_DEBUG_TYPE_PUSH_GROUP: ss << "PUSH GROUP - Debug group pushed\n"; break;
		case GL_DEBUG_TYPE_POP_GROUP: ss << "POP GROUP - Debug group popped\n"; break;
		case GL_DEBUG_TYPE_OTHER: ss << "OTHER - Other type of message\n"; break;
		default: ss << "Unknown type: " << std::hex << type << std::dec << "\n"; break;
	}

	// Severity
	ss << "\n--- SEVERITY ---\n";
	switch (severity) {
		case GL_DEBUG_SEVERITY_HIGH: ss << "[HIGH] Severe issue that may cause incorrect rendering or crashes\n"; break;
		case GL_DEBUG_SEVERITY_MEDIUM: ss << "[MEDIUM] Significant issue that should be addressed\n"; break;
		case GL_DEBUG_SEVERITY_LOW: ss << "[LOW] Minor issue with little impact\n"; break;
		case GL_DEBUG_SEVERITY_NOTIFICATION: ss << "[NOTIFICATION] Informational message\n"; break;
		default: ss << "Unknown severity: " << std::hex << severity << std::dec << "\n"; break;
	}

	// Additional OpenGL State Information
	ss << "\n--- OPENGL STATE ---\n";

	GLint activeTexture;
	glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
	ss << "Active Texture Unit: GL_TEXTURE" << (activeTexture - GL_TEXTURE0) << "\n";

	GLint boundTexture2D;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTexture2D);
	ss << "Bound 2D Texture: " << boundTexture2D << "\n";

	GLint boundArrayBuffer;
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &boundArrayBuffer);
	ss << "Bound Array Buffer: " << boundArrayBuffer << "\n";

	GLint boundElementBuffer;
	glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &boundElementBuffer);
	ss << "Bound Element Array Buffer: " << boundElementBuffer << "\n";

	GLint boundFramebuffer;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &boundFramebuffer);
	ss << "Bound Framebuffer: " << boundFramebuffer << "\n";

	GLint currentProgram;
	glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
	ss << "Current Shader Program: " << currentProgram << "\n";

	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	ss << "Viewport: x=" << viewport[0] << " y=" << viewport[1] << " width=" << viewport[2] << " height=" << viewport[3] << "\n";

	GLboolean depthTest, blending, scissorTest, cullFace;
	glGetBooleanv(GL_DEPTH_TEST, &depthTest);
	glGetBooleanv(GL_BLEND, &blending);
	glGetBooleanv(GL_SCISSOR_TEST, &scissorTest);
	glGetBooleanv(GL_CULL_FACE, &cullFace);
	ss << "Depth Test: " << (depthTest ? "ENABLED" : "DISABLED") << "\n";
	ss << "Blending: " << (blending ? "ENABLED" : "DISABLED") << "\n";
	ss << "Scissor Test: " << (scissorTest ? "ENABLED" : "DISABLED") << "\n";
	ss << "Face Culling: " << (cullFace ? "ENABLED" : "DISABLED") << "\n";

	GLint depthFunc, blendSrcRGB, blendDstRGB;
	glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
	glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRGB);
	glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRGB);
	ss << "Depth Function: " << std::hex << depthFunc << std::dec << "\n";
	ss << "Blend Src RGB: " << std::hex << blendSrcRGB << std::dec << " | Blend Dst RGB: " << std::hex << blendDstRGB << std::dec << "\n";

	ss << "\n================================================================================\n\n";

	TOAST_TRACE("{0}", ss.str());
}
#endif

OpenGLRenderer::OpenGLRenderer() {
	PROFILE_ZONE_N("OpenGL Renderer Construction");
	TOAST_INFO("Initializing OpenGL Renderer");

	if (!m_instance) {
		m_instance = this;
	}

	// Load OpenGL functions using GLAD
	int version = gladLoadGL(SDL_GL_GetProcAddress);
	if (!version) {
		TOAST_ERROR("Failed to initialize OpenGL context");
	}
	// Successfully loaded OpenGL
	TOAST_INFO("Loaded OpenGL {0}.{1}", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));

#ifdef TRACY_ENABLE
	TracyGpuContext;
#endif

	// opengl configuration
	glClearColor(0.1f, 0.1f, 0.1f, 1.0f);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_SCISSOR_TEST);
	glEnable(GL_MULTISAMPLE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);

	// Load renderer settings before creating framebuffer resources that depend on config values.
	LoadRenderSettings();

	// Debug output
#ifndef NDEBUG
	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(DebugCallback, nullptr);
#endif

	// Default projection matrix
	SetProjectionMatrix(glm::radians(90.0f), 16.0f / 9.0f, 0.1f, 1000.0f);

	GLint maxSupportedSamples = 1;
	glGetIntegerv(GL_MAX_SAMPLES, &maxSupportedSamples);
	const unsigned requestedSamples = std::max(1u, m_config.msaaSamples);
	const unsigned clampedSamples = static_cast<unsigned>(std::min(static_cast<GLint>(requestedSamples), maxSupportedSamples));
	if (requestedSamples != clampedSamples) {
		TOAST_WARN("Requested MSAA sample count {0} exceeds GPU limit {1}. Clamping.", requestedSamples, clampedSamples);
	}
	m_config.msaaSamples = clampedSamples;

	// Geometry framebuffer
	Framebuffer::Specs geometrySpecs = { 1920, 1080, /*clampedSamples > 1*/ false };
	m_geometryFramebuffer = new Framebuffer(geometrySpecs);
	m_geometryFramebuffer->AddColorAttachment(GL_RGBA16F, GL_RGBA, GL_FLOAT);    // albedo HDR buffer
	m_geometryFramebuffer->AddDepthAttachment();
	m_geometryFramebuffer->Build();

	Framebuffer::Specs lightSpecs = { (int)(1920 * m_config.lightResolutionScale), (int)(1080 * m_config.lightResolutionScale), false };
	m_lightFramebuffer = new Framebuffer(lightSpecs);
	m_lightFramebuffer->AddColorAttachment(GL_RGBA16F, GL_RGBA, GL_FLOAT);    // light accumulation buffer
	m_lightFramebuffer->AddDepthAttachment();
	m_lightFramebuffer->Build();

	m.postProcessFramebuffer = new Framebuffer(geometrySpecs);
	m.postProcessFramebuffer->AddColorAttachment(GL_RGBA16F, GL_RGBA, GL_FLOAT);
	m.postProcessFramebuffer->Build();

	Framebuffer::Specs outputSpecs = { 1920, 1080, false };
	m_outputFramebuffer = new Framebuffer(outputSpecs);
	m_outputFramebuffer->AddColorAttachment(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);    // final output buffer
	m_outputFramebuffer->Build();

	// Framebuffer::Specs resolveSpecs = { 1920, 1080 };
	// m_geometryResolveFramebuffer = new Framebuffer(resolveSpecs);
	// m_geometryResolveFramebuffer->AddColorAttachment(GL_RGBA16F, GL_RGBA, GL_FLOAT);
	// m_geometryResolveFramebuffer->Build();

	RecreateShadowResources(m_config.shadowMapResolution);

	m.sdfComputeShader = resource::LoadResource<Shader>("SHADERS/occlusionSDF.shader");
	m.jfaInitComputeShader = resource::LoadResource<Shader>("SHADERS/occlusionJFAInit.shader");
	m.jfaComputeShader = resource::LoadResource<Shader>("SHADERS/occlusionJFA.shader");
	m.finalComputeShader = resource::LoadResource<Shader>("SHADERS/occlusionFinal.shader");

	// post process
	m_postProcessManager = std::make_unique<PostProcessManager>();
	m_postProcessManager->InitBuffers(m_config.resolution);

	std::unique_ptr<Tonemaping> tonemapping = std::make_unique<Tonemaping>();
	std::unique_ptr<Colorgrading> colorgrading = std::make_unique<Colorgrading>();
	m_postProcessManager->AddGlobalProcess(std::move(tonemapping));
	m_postProcessManager->AddGlobalProcess(std::move(colorgrading));

	// Listen to window resize events
	m_listener.Subscribe<event::WindowResize>([this](event::WindowResize* e) -> bool {
		if (e->width == 0 || e->height == 0) {
			return true;
		}
		// Update viewport and projection matrix
		Resize({ static_cast<unsigned>(e->width), static_cast<unsigned>(e->height) });
		return true;
	});

	m_listener.Subscribe<event::WindowKey>([this](event::WindowKey* e) -> bool {
		// Toggle Fullscreen F11
		if (e->key == SDLK_F11 && e->action == event::WINDOW_INPUT_PRESSED) {
			ToggleFullscreen();
		}

		return false;
	});

	// call resize once at start
	{
		auto* window = toast::Window::GetInstance();
		OpenGLRenderer::Resize(window->GetFramebufferSize());
	}

#ifdef TOAST_EDITOR
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;    // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;        // IF using Docking Branch
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;      // Enable Multi-Viewport

	// Setup Platform/Renderer backends
	ImGui_ImplSDL3_InitForOpenGL(toast::Window::GetInstance()->GetWindow(), toast::Window::GetInstance()->GetGLContext());
	ImGui_ImplOpenGL3_Init();

	g_backup_current_window = SDL_GL_GetCurrentWindow();
	g_backup_current_context = SDL_GL_GetCurrentContext();
#endif

	// setup layerstack
	m.layerStack = LayerStack::GetInstance();
	// TOAST_ASSERT(m_layerStack != nullptr, "LayerStack must be created before OpenGLRenderer!");

	// setup default resources
	m.quad = resource::LoadResource<Mesh>("MODELS/quad.obj");
	m.screenShader = resource::LoadResource<Shader>("SHADERS/screen.shader");
	m.flippedScreenShader = resource::LoadResource<Shader>("SHADERS/flippedScreen.shader");
	m.combineLightShader = resource::LoadResource<Shader>("SHADERS/combineLight.shader");
	m.globalLightShader = resource::LoadResource<Shader>("SHADERS/globalLight.shader");

	// Set once, change and reset state if needed
	stbi_set_flip_vertically_on_load_thread(1);
}

OpenGLRenderer::~OpenGLRenderer() {
	TOAST_INFO("Shutting down OpenGL Renderer...");

	delete m_geometryFramebuffer;
	delete m_outputFramebuffer;
	DestroyShadowResources();
#ifdef TOAST_EDITOR
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();
#endif
}

void OpenGLRenderer::StartImGuiFrame() {
#ifdef TOAST_EDITOR
	PROFILE_ZONE;
#ifdef TRACY_ENABLE
	TracyGpuZone("ImGuiStart");
#endif
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();
	ImGuizmo::SetOrthographic(false);
	ImGuizmo::BeginFrame();

#endif
}

void OpenGLRenderer::EndImGuiFrame() {
#ifdef TOAST_EDITOR
	PROFILE_ZONE;
#ifdef TRACY_ENABLE
	TracyGpuZone("ImGuiEnd");
#endif

	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	// Only render platform windows when the feature is enabled in IO flags
	ImGuiIO& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
		SDL_GL_MakeCurrent(g_backup_current_window, g_backup_current_context);
	}

#ifdef TRACY_ENABLE
	TracyGpuCollect;
#endif

#endif
}

void OpenGLRenderer::Render() {
	if (toast::Window::GetInstance()->IsMinimized()) {
		return;
	}
	PROFILE_ZONE;

#ifdef TRACY_ENABLE
	TracyGpuZone("Main Render");
#endif

	// Update view matrix only when a camera is active
	if (m_activeCamera) {
		SetViewMatrix(m_activeCamera->GetViewMatrix());
	}

	// Compute combined matrix once
	m_multipliedMatrix = m_projectionMatrix * m_viewMatrix;

	// Extract frustum planes for culling
	OclussionVolume::extractFrustumPlanesNormalized(m_multipliedMatrix, m_frustumPlanes);

	// Rebuild enabled renderables only when scene membership or enable-state changes.
	if (m_renderablesSortDirty) {
		m.combinedRenderables.clear();
		m.combinedRenderables.reserve(m_renderables.size());
		for (auto* r : m_renderables) {
			if (r && m_disabledRenderables.find(r) == m_disabledRenderables.end()) {
				m.combinedRenderables.push_back(r);
			}
		}
		m_renderablesSortDirty = false;
	}

	// Depth can change every frame, so keep sort per-frame but only for enabled list.
	// HACK: FOR OPTIMIZATION PURPOSES WE JUST SORT WHEN ADDING OBJECTS
	if (m.combinedRenderables.size() > 1) {
		std::stable_sort(m.combinedRenderables.begin(), m.combinedRenderables.end(), [](IRenderable* a, IRenderable* b) {
			return a->GetDepth() < b->GetDepth();
		});
	}
	
	if (m_transparentsSortDirty) {
		m.combinedTransparents.clear();
		m.combinedTransparents.reserve(m_transparentRenderables.size());
		for (auto* r : m_transparentRenderables) {
			if (r && m_disabledTransparents.find(r) == m_disabledTransparents.end()) {
				m.combinedTransparents.push_back(r);
			}
		}
		m_renderablesSortDirty = false;
		
		std::stable_sort(m.combinedTransparents.begin(), m.combinedTransparents.end(), [](IRenderable* a, IRenderable* b) {
			return a->GetDepth() < b->GetDepth();
		});
		
	}

	OcclusionPass();
	CHECK_GL();

	GeometryPass();
	CHECK_GL();

	SpritePass();
	CHECK_GL();

	// Lighting
	LightingPass();

	PostProcessPass();
	CHECK_GL();

	// Render non-HUD editor/game layers into a dedicated layer framebuffer
	if (m.layerStack) {
		for (auto* layer : m.layerStack->GetLayers()) {
			if (auto* hud = dynamic_cast<HUD::HUDLayer*>(layer)) {
				auto* fb = hud->GetFramebuffer();
				if (fb) {
					fb->bind();
					glViewport(0, 0, fb->Width(), fb->Height());
					glScissor(0, 0, fb->Width(), fb->Height());
					glClearColor(0, 0, 0, 0);
					glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
					Framebuffer::unbind();
				}
			}
		}
	}

	m_outputFramebuffer->bind();
	glViewport(0, 0, m_outputFramebuffer->Width(), m_outputFramebuffer->Height());
	glScissor(0, 0, m_outputFramebuffer->Width(), m_outputFramebuffer->Height());
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LEQUAL);

	if (m.layerStack) {
		m.layerStack->RenderLayers();
	}
	Framebuffer::unbind();

	HUDPass();
	CHECK_GL();    // Added missing semicolon

#ifndef TOAST_EDITOR
	{
#ifdef TRACY_ENABLE
		TracyGpuZone("ScreenPass");
#endif
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glDisable(GL_DEPTH_TEST);
		glClear(GL_COLOR_BUFFER_BIT);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_outputFramebuffer->GetColorTexture(0));

		m.screenShader->Use();
		m.screenShader->SetSampler("screenTexture", 0);
		m.quad->Draw();

		glEnable(GL_DEPTH_TEST);
	}
#endif

#ifndef TOAST_EDITOR
#ifdef TRACY_ENABLE
	TracyGpuCollect;
#endif
#endif
}

void OpenGLRenderer::GeometryPass() {
#ifdef TRACY_ENABLE
	PROFILE_ZONE;
	TracyGpuZone("Geometry Pass");
#endif

	glViewport(0, 0, m_geometryFramebuffer->Width(), m_geometryFramebuffer->Height());
	glScissor(0, 0, m_geometryFramebuffer->Width(), m_geometryFramebuffer->Height());

	m_geometryFramebuffer->bind();

	glDepthRange(0.0, 1.0);

	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LEQUAL);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	Clear();

	for (auto* r : m.combinedRenderables) {
		r->OnRender(renderer::IRenderablePass::GEOMETRY, m_multipliedMatrix);
	}
}

void OpenGLRenderer::OcclusionPass() {
#ifdef TRACY_ENABLE
	PROFILE_ZONE;
	TracyGpuZone("Occlusion Pass");
#endif

	m.occlusionFramebuffer->bind();
	glViewport(0, 0, m.occlusionFramebuffer->Width(), m.occlusionFramebuffer->Height());
	glScissor(0, 0, m.occlusionFramebuffer->Width(), m.occlusionFramebuffer->Height());

	glClear(GL_COLOR_BUFFER_BIT);

	glDisable(GL_BLEND);

	for (auto* r : m.combinedRenderables) {
		r->OnRender(renderer::IRenderablePass::OCCLUSION, m_multipliedMatrix);
	}
	for (auto* r : m.combinedTransparents) {
		r->OnRender(renderer::IRenderablePass::OCCLUSION, m_multipliedMatrix);
	}

	m.jfaInitComputeShader->Use();
	glBindTextureUnit(0, m.occlusionFramebuffer->GetColorTexture(0));
	glBindImageTexture(1, m.jfaTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
	m.jfaInitComputeShader->Set("resolution", glm::ivec2(m.occlusionFramebuffer->Width(), m.occlusionFramebuffer->Height()));
	glDispatchCompute((m.occlusionFramebuffer->Width() + 15) / 16, (m.occlusionFramebuffer->Height() + 15) / 16, 1);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	GLuint inputTex = m.jfaTex;
	GLuint outputTex = m.pingPongTex;

	int maxDim = std::max(m.occlusionFramebuffer->Width(), m.occlusionFramebuffer->Height());
	int step = maxDim / 2;

	while (step > 0) {
		m.jfaComputeShader->Use();
		m.jfaComputeShader->Set("resolution", glm::ivec2(m.occlusionFramebuffer->Width(), m.occlusionFramebuffer->Height()));
		m.jfaComputeShader->Set("stepValue", step);

		glBindTextureUnit(0, inputTex);
		glBindImageTexture(1, outputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);

		glDispatchCompute((m.occlusionFramebuffer->Width() + 15) / 16, (m.occlusionFramebuffer->Height() + 15) / 16, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		std::swap(inputTex, outputTex);

		step /= 2;
	}

	m.finalComputeShader->Use();
	glBindTextureUnit(0, inputTex);    // last JFA result
	glBindImageTexture(1, m.sdfTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
	m.finalComputeShader->Set("resolution", glm::ivec2(m.occlusionFramebuffer->Width(), m.occlusionFramebuffer->Height()));
	glDispatchCompute((m.occlusionFramebuffer->Width() + 15) / 16, (m.occlusionFramebuffer->Height() + 15) / 16, 1);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void OpenGLRenderer::PostProcessPass() {
	m_postProcessManager->PostProcessPass(m.postProcessFramebuffer, m_outputFramebuffer);
}

void OpenGLRenderer::LightingPass() {
#ifdef TRACY_ENABLE
	PROFILE_ZONE;
	TracyGpuZone("Lighting Pass");
#endif

	glViewport(0, 0, m_lightFramebuffer->Width(), m_lightFramebuffer->Height());
	glScissor(0, 0, m_lightFramebuffer->Width(), m_lightFramebuffer->Height());

	if (m_lightsSortDirty && m_lights.size() > 1) {
		std::ranges::stable_sort(m_lights, [](Light2D* a, Light2D* b) {
			return a->transform()->position().z < b->transform()->position().z;
		});
		m_lightsSortDirty = false;
	}

	m_lightFramebuffer->bind();
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	m_geometryFramebuffer->bindRead();
	m_lightFramebuffer->bindDraw();
	glBlitFramebuffer(
	    0,
	    0,
	    m_geometryFramebuffer->Width(),
	    m_geometryFramebuffer->Height(),
	    0,
	    0,
	    m_lightFramebuffer->Width(),
	    m_lightFramebuffer->Height(),
	    GL_DEPTH_BUFFER_BIT,
	    GL_NEAREST
	);
	m_lightFramebuffer->bind();

	glDepthMask(GL_FALSE);

	glDisable(GL_DEPTH_TEST);

	if (!m_lights.empty()) {
		for (auto* light : m_lights) {
			if (light) {
				light->OnRender(m_multipliedMatrix);
			}
		}
	}

	m.postProcessFramebuffer->bind();
	glDisable(GL_BLEND);

	glViewport(0, 0, m.postProcessFramebuffer->Width(), m.postProcessFramebuffer->Height());
	glScissor(0, 0, m.postProcessFramebuffer->Width(), m.postProcessFramebuffer->Height());

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_geometryFramebuffer->GetColorTexture(0));

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, m_lightFramebuffer->GetColorTexture(0));

	m.combineLightShader->Use();
	m.combineLightShader->SetSampler("gAlbedoTexture", 0);
	m.combineLightShader->SetSampler("gLightingTexture", 1);
	m.combineLightShader->Set("gGlobalLightIntensity", m_globalLightIntensity);
	m.combineLightShader->Set("gGlobalLightColor", m_globalLightColor);
	m.quad->Draw();

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenGLRenderer::Clear() const {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGLRenderer::Resize(glm::uvec2 size) {
	unsigned width = size.x;
	unsigned height = size.y;

	unsigned geometryWidth = std::max(1u, static_cast<unsigned>(width * m_config.resolutionScale));
	unsigned geometryHeight = std::max(1u, static_cast<unsigned>(height * m_config.resolutionScale));

	m_geometryFramebuffer->Resize(static_cast<int>(geometryWidth), static_cast<int>(geometryHeight));

	m_lightFramebuffer->Resize(
	    static_cast<unsigned int>(width * m_config.lightResolutionScale), static_cast<unsigned int>(height * m_config.lightResolutionScale)
	);
	m_outputFramebuffer->Resize(static_cast<int>(width), static_cast<int>(height));
	m_postProcessManager->InitBuffers(size);

	m.postProcessFramebuffer->Resize(static_cast<int>(width), static_cast<int>(height));

	if (m.layerStack) {
		for (auto* layer : m.layerStack->GetLayers()) {
			if (auto* hudLayer = dynamic_cast<renderer::HUD::HUDLayer*>(layer)) {
				hudLayer->Resize(width, height);
			}
		}
	}

	SetProjectionMatrix(glm::radians(90.0f), static_cast<float>(width) / static_cast<float>(height), 0.1f, 1000.0f);

	auto windowFramebufferSize = toast::Window::GetInstance()->GetFramebufferSize();
	if (windowFramebufferSize.x == width && windowFramebufferSize.y == height) {
		m_config.resolution = glm::uvec2(width, height);
	}
}

void OpenGLRenderer::AddRenderable(IRenderable* renderable) {
	if (!renderable) {
		return;
	}
	m_renderables.push_back(renderable);
	m_renderablesSortDirty = true;
}

void OpenGLRenderer::RemoveRenderable(IRenderable* renderable) {
	auto it = std::find(m_renderables.begin(), m_renderables.end(), renderable);
	if (it != m_renderables.end()) {
		m_renderables.erase(it);
	}
	m_disabledRenderables.erase(renderable);    // clean up if it was disabled
	m_renderablesSortDirty = true;
}

void OpenGLRenderer::AddTransparent(IRenderable* renderable) {
	m_transparentRenderables.push_back(renderable);
}

void OpenGLRenderer::RemoveTransparent(IRenderable* renderable) {
	auto it = std::find(m_transparentRenderables.begin(), m_transparentRenderables.end(), renderable);
	if (it != m_transparentRenderables.end()) {
		m_transparentRenderables.erase(it);
	}
	m_disabledTransparents.erase(renderable);
	m_transparentsSortDirty = true;
}

void OpenGLRenderer::SpritePass() {
	for (const auto& renderable : m.combinedTransparents) {
		renderable->OnRender(IRenderablePass::GEOMETRY, m_multipliedMatrix);
	}
}

void OpenGLRenderer::AddLight(Light2D* light) {
	m_lights.push_back(light);
	m_lightsSortDirty = true;
}

void OpenGLRenderer::RemoveLight(Light2D* light) {
	auto it = std::find(m_lights.begin(), m_lights.end(), light);
	if (it != m_lights.end()) {
		m_lights.erase(it);
	}
	m_lightsSortDirty = true;
}

void OpenGLRenderer::ApplyRenderSettings() {
	auto window = toast::Window::GetInstance();

	window->SetVSync(m_config.vSync);
	window->SetRefreshFrameTime(1000.0 / m_config.maxFPS);
	window->SetDisplayMode(m_config.currentDisplayMode);
	window->SetResolution(m_config.resolution);

	GLint maxSupportedSamples = 1;
	glGetIntegerv(GL_MAX_SAMPLES, &maxSupportedSamples);
	const unsigned requestedSamples = std::max(1u, m_config.msaaSamples);
	const unsigned clampedSamples = static_cast<unsigned>(std::min(static_cast<GLint>(requestedSamples), maxSupportedSamples));
	if (requestedSamples != clampedSamples) {
		TOAST_WARN("Requested MSAA sample count {0} exceeds GPU limit {1}. Clamping.", requestedSamples, clampedSamples);
	}
	m_config.msaaSamples = clampedSamples;

	if (!m_geometryFramebuffer || static_cast<unsigned>(m_geometryFramebuffer->Samples()) != clampedSamples) {
		const unsigned outputWidth = std::max(1, m_outputFramebuffer ? m_outputFramebuffer->Width() : 1920);
		const unsigned outputHeight = std::max(1, m_outputFramebuffer ? m_outputFramebuffer->Height() : 1080);
		const unsigned geometryWidth = std::max(1u, static_cast<unsigned>(outputWidth * m_config.resolutionScale));
		const unsigned geometryHeight = std::max(1u, static_cast<unsigned>(outputHeight * m_config.resolutionScale));

		delete m_geometryFramebuffer;
		Framebuffer::Specs geometrySpecs = { static_cast<int>(geometryWidth),
			                                   static_cast<int>(geometryHeight),
			                                   /*clampedSamples > 1*/ false,
			                                   static_cast<int>(clampedSamples) };
		m_geometryFramebuffer = new Framebuffer(geometrySpecs);
		m_geometryFramebuffer->AddColorAttachment(GL_RGBA16F, GL_RGBA, GL_FLOAT);
		m_geometryFramebuffer->AddDepthAttachment();
		m_geometryFramebuffer->Build();
	}

	m_lightFramebuffer->Resize(
	    m_geometryFramebuffer->Width() * m_config.lightResolutionScale, m_geometryFramebuffer->Height() * m_config.lightResolutionScale
	);

	m.postProcessFramebuffer->Resize(m_geometryFramebuffer->Width(), m_geometryFramebuffer->Height());

	if (!m.occlusionFramebuffer || static_cast<unsigned>(m.occlusionFramebuffer->Width()) != m_config.shadowMapResolution ||
	    static_cast<unsigned>(m.occlusionFramebuffer->Height()) != m_config.shadowMapResolution) {
		RecreateShadowResources(m_config.shadowMapResolution);
	}

	Resize(window->GetFramebufferSize());

	SaveRenderSettings();
}

void OpenGLRenderer::DestroyShadowResources() {
	delete m.occlusionFramebuffer;
	m.occlusionFramebuffer = nullptr;

	if (m.jfaTex) {
		glDeleteTextures(1, &m.jfaTex);
		m.jfaTex = 0;
	}
	if (m.pingPongTex) {
		glDeleteTextures(1, &m.pingPongTex);
		m.pingPongTex = 0;
	}
	if (m.sdfTex) {
		glDeleteTextures(1, &m.sdfTex);
		m.sdfTex = 0;
	}
}

void OpenGLRenderer::RecreateShadowResources(unsigned resolution) {
	const unsigned clampedResolution = std::clamp(resolution, 64u, 8192u);
	m_config.shadowMapResolution = clampedResolution;

	DestroyShadowResources();

	Framebuffer::Specs occlusionSpecs = { static_cast<int>(clampedResolution), static_cast<int>(clampedResolution) };
	m.occlusionFramebuffer = new Framebuffer(occlusionSpecs);
	m.occlusionFramebuffer->AddColorAttachment(GL_R8, GL_RED, GL_UNSIGNED_BYTE);
	m.occlusionFramebuffer->Build();

	glGenTextures(1, &m.jfaTex);
	glBindTexture(GL_TEXTURE_2D, m.jfaTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, m.occlusionFramebuffer->Width(), m.occlusionFramebuffer->Height(), 0, GL_RG, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glGenTextures(1, &m.pingPongTex);
	glBindTexture(GL_TEXTURE_2D, m.pingPongTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, m.occlusionFramebuffer->Width(), m.occlusionFramebuffer->Height(), 0, GL_RG, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glGenTextures(1, &m.sdfTex);
	glBindTexture(GL_TEXTURE_2D, m.sdfTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, m.occlusionFramebuffer->Width(), m.occlusionFramebuffer->Height(), 0, GL_RED, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenGLRenderer::DrawScreenQuad(bool flipY, bool useShader) {
	if (useShader) {
		if (flipY) {
			m.flippedScreenShader->Use();
			m.flippedScreenShader->SetSampler("screenTexture", 0);
		} else {
			m.screenShader->Use();
			m.screenShader->SetSampler("screenTexture", 0);
		}
	}
	m.quad->Draw();
}

GLuint OpenGLRenderer::GetShadowMapTexture() const {
	return m.sdfTex;
}

void OpenGLRenderer::HUDPass() {
	PROFILE_ZONE;
#ifdef TRACY_ENABLE
	TracyGpuZone("HUD Pass");
#endif

	if (!m.layerStack) {
		return;
	}

	bool anyHudDrawn = false;
	for (auto* layer : m.layerStack->GetLayers()) {
		auto* hud = dynamic_cast<HUD::HUDLayer*>(layer);
		if (!hud || !hud->GetFramebuffer()) {
			continue;
		}

		GLuint hudTex = hud->GetFramebuffer()->GetColorTexture(0);
		if (hudTex == 0) {
			continue;
		}

		if (!anyHudDrawn) {
			m_outputFramebuffer->bind();
			glDrawBuffer(GL_COLOR_ATTACHMENT0);
			glViewport(0, 0, m_outputFramebuffer->Width(), m_outputFramebuffer->Height());
			glScissor(0, 0, m_outputFramebuffer->Width(), m_outputFramebuffer->Height());

			glDisable(GL_DEPTH_TEST);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			m.screenShader->Use();
		}

		anyHudDrawn = true;

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, hudTex);
		m.screenShader->SetSampler("screenTexture", 0);

		m.quad->Draw();
	}

	if (!anyHudDrawn) {
		return;
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);

	Framebuffer::unbind();
}

}
