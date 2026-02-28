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
#include "Toast/Time.hpp"
#include "Toast/Window/Window.hpp"
#include "Toast/Window/WindowEvents.hpp"

#include <GLFW/glfw3.h>
#include <glad/gl.h>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifdef TOAST_EDITOR
// clang-format off
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <ImGuizmo.h>
// clang-format on
#endif

#include "Toast/Renderer/IRenderable.hpp"
#include "Toast/Renderer/OclussionVolume.hpp"

#include <sstream>
#include <stb_image.h>

#ifdef TRACY_ENABLE
#include <tracy/TracyOpenGL.hpp>
#endif

namespace renderer {

IRendererBase* IRendererBase::m_instance = nullptr;

#ifdef TOAST_EDITOR
static GLFWwindow* g_backup_current_context = nullptr;
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
	int version = gladLoadGL(glfwGetProcAddress);
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
	// glEnable(GL_CULL_FACE);
	// glCullFace(GL_BACK);
	// glFrontFace(GL_CCW);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Debug output
#ifndef NDEBUG
	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(DebugCallback, nullptr);
#endif

	// Default projection matrix
	SetProjectionMatrix(glm::radians(90.0f), 16.0f / 9.0f, 0.1f, 1000.0f);

	// Geometry framebuffer
	Framebuffer::Specs s = { 1920, 1080 };
	m_geometryFramebuffer = new Framebuffer(s);
	m_geometryFramebuffer->AddColorAttachment(GL_RGBA16F, GL_RGBA, GL_FLOAT);    // albedo HDR buffer
	// m_geometryFramebuffer->AddColorAttachment(GL_RGBA16F, GL_RGBA, GL_FLOAT);    // position HDR buffer
	m_geometryFramebuffer->AddColorAttachment(GL_RGBA16F, GL_RGBA, GL_FLOAT);    // normals HDR buffer
	m_geometryFramebuffer->AddDepthAttachment();
	m_geometryFramebuffer->Build();

	m_lightFramebuffer = new Framebuffer(s);
	m_lightFramebuffer->AddColorAttachment(GL_RGBA16F, GL_RGBA, GL_FLOAT);    // light accumulation buffer
	m_lightFramebuffer->AddColorAttachment(GL_RGBA16F, GL_RGBA, GL_FLOAT);    // normal buffer
	m_lightFramebuffer->AddDepthAttachment();
	m_lightFramebuffer->Build();

	m_outputFramebuffer = new Framebuffer(s);
	m_outputFramebuffer->AddColorAttachment(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);    // final output buffer
	m_outputFramebuffer->AddColorAttachment(GL_RGBA16F, GL_RGBA, GL_FLOAT);          // lighting info
	m_outputFramebuffer->Build();

	// Layer framebuffer: used to render game/editor layers (non-HUD) before compositing
	m_layerFramebuffer = new Framebuffer(s);
	m_layerFramebuffer->AddColorAttachment(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);    // layer colors (same format as output)
	m_layerFramebuffer->AddDepthAttachment();                                       // allow depth testing for layers
	m_layerFramebuffer->Build();

	//@TODO: HDR support

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
		if (e->key == GLFW_KEY_F11 && e->action == GLFW_PRESS) {
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
	// io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;     // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // IF using Docking Branch
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;    // Enable Multi-Viewport

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForOpenGL(
	    toast::Window::GetInstance()->GetWindow(), true
	);    // Second param install_callback=true will install GLFW callbacks and chain to existing ones.
	ImGui_ImplOpenGL3_Init();

	g_backup_current_context = glfwGetCurrentContext();
#endif

	// setup layerstack
	m_layerStack = LayerStack::GetInstance();
	// TOAST_ASSERT(m_layerStack != nullptr, "LayerStack must be created before OpenGLRenderer!");

	// setup default resources
	m_quad = resource::LoadResource<Mesh>("MODELS/quad.obj");
	m_screenShader = resource::LoadResource<Shader>("SHADERS/screen.shader");
	m_combineLightShader = resource::LoadResource<Shader>("SHADERS/combineLight.shader");
	m_globalLightShader = resource::LoadResource<Shader>("SHADERS/globalLight.shader");

	// Set once, change and reset state if needed
	stbi_set_flip_vertically_on_load(1);

	// Load settings
	LoadRenderSettings();
}

OpenGLRenderer::~OpenGLRenderer() {
	TOAST_INFO("Shutting down OpenGL Renderer...");

	delete m_geometryFramebuffer;
	delete m_lightFramebuffer;
	delete m_outputFramebuffer;
	delete m_layerFramebuffer;
#ifdef TOAST_EDITOR
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
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
	ImGui_ImplGlfw_NewFrame();
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
		glfwMakeContextCurrent(g_backup_current_context);
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

	// ParticleSystems are updated by the scene/world tick; renderer doesn't tick them directly

	// Geometry
	GeometryPass();

	// Transparent/sprite pass: render into the geometry (G-)buffer so transparents are affected by lighting
	SpritePass();

	// Lighting
	LightingPass();

	// Combine (writes into m_outputFramebuffer)
	CombinedRenderPass();

	// Render game/editor layers directly into the output framebuffer (HUD layers still render into their own FBOs)
	m_outputFramebuffer->bind();
	// ensure viewport/scissor match output
	glViewport(0, 0, m_outputFramebuffer->Width(), m_outputFramebuffer->Height());
	glScissor(0, 0, m_outputFramebuffer->Width(), m_outputFramebuffer->Height());

	// Render non-HUD editor/game layers into a dedicated layer framebuffer
	// Clear HUD layer framebuffers first to avoid persistence/smearing if a HUD view didn't draw pixels this frame
	if (m_layerStack) {
		for (auto* layer : m_layerStack->GetLayers()) {
			if (auto* hud = dynamic_cast<HUD::HUDLayer*>(layer)) {
				if (auto* fb = hud->GetFramebuffer()) {
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

	// Now bind the per-layer framebuffer and clear it (transparent)
	m_layerFramebuffer->bind();
	glViewport(0, 0, m_layerFramebuffer->Width(), m_layerFramebuffer->Height());
	glScissor(0, 0, m_layerFramebuffer->Width(), m_layerFramebuffer->Height());
	// Ensure depth testing is enabled for layer rendering and depth buffer is writable
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LEQUAL);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// Render layers; HUD layers will still render into their own FBOs
	m_layerStack->RenderLayers();
	Framebuffer::unbind();

	// Composite layer framebuffer over the combined output (alpha blend)
	m_outputFramebuffer->bind();
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	m_screenShader->Use();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_layerFramebuffer->GetColorTexture(0));
	m_screenShader->SetSampler("screenTexture", 0);
	m_quad->Draw();
	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	Framebuffer::unbind();

	// Composite all HUD layer framebuffers onto the output as the final pass
	HUDPass();

// draw to screen only if not in editor mode
#ifndef TOAST_EDITOR
	{
#ifdef TRACY_ENABLE
		TracyGpuZone("ScreenPass");
#endif
		// Present final framebuffer to default framebuffer
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		// We control depth testing. Disable for fullscreen quad, then enable back.
		glDisable(GL_DEPTH_TEST);
		glClear(GL_COLOR_BUFFER_BIT);

		// bind texture
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_outputFramebuffer->GetColorTexture(0));

		// draw screen quad
		m_screenShader->Use();
		m_screenShader->SetSampler("screenTexture", 0);
		m_quad->Draw();

		// Restore depth test
		glEnable(GL_DEPTH_TEST);
	}
#endif

#ifndef TOAST_EDITOR
#ifdef TRACY_ENABLE
	// Collect GPU queries in non-editor builds too
	TracyGpuCollect;
#endif
#endif
}

void OpenGLRenderer::GeometryPass() {
#ifdef TRACY_ENABLE
	PROFILE_ZONE;
	TracyGpuZone("Geometry Pass");
#endif

	// Sort by depth (front-to-back) only when list has changed
	if (m_renderablesSortDirty && m_renderables.size() > 1) {
		std::ranges::stable_sort(m_renderables, [](IRenderable* a, IRenderable* b) {
			return a->GetDepth() < b->GetDepth();
		});
		m_renderablesSortDirty = false;
	}

	// Prepare render target and GL state
	glViewport(0, 0, m_geometryFramebuffer->Width(), m_geometryFramebuffer->Height());
	glScissor(0, 0, m_geometryFramebuffer->Width(), m_geometryFramebuffer->Height());

	m_geometryFramebuffer->bind();

	// Ensure depth mapping is normalized (avoid driver-specific differences)
	glDepthRange(0.0, 1.0);

	// {
	// 	GLenum draws[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	// 	glDrawBuffers(2, draws);
	// }

	// Ensure depth and face-culling state is correct for geometry rendering
	glDisable(GL_CULL_FACE);    // avoid culling if winding conventions differ
	glDisable(GL_BLEND);        // Geometry should write depth, not blended
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LEQUAL);    // allow equal depth to pass for some projection conventions
	glClearDepth(1.0);

	// Clear both color and depth
	Clear();

	// geometry pass
	for (auto* r : m_renderables) {
		if (r) {
			r->OnRender(m_multipliedMatrix);
		}
	}

	// Restore predictable state (depth writes on)
	glDepthMask(GL_TRUE);
	glEnable(GL_BLEND);

	// Don't unbind here - will be unbound when next framebuffer is bound
}

void OpenGLRenderer::LightingPass() {
#ifdef TRACY_ENABLE
	PROFILE_ZONE;
	TracyGpuZone("Lighting Pass");
#endif

	// If global lighting is disabled, avoid all light buffer work
	if (!m_globalLightEnabled) {
		return;
	}

	// Sort lights by z to ensure correct accumulation ordering when needed
	if (m_lightsSortDirty && m_lights.size() > 1) {
		std::ranges::stable_sort(m_lights, [](Light2D* a, Light2D* b) {
			return a->transform()->position().z < b->transform()->position().z;
		});
		m_lightsSortDirty = false;
	}

	// Render lights at their own resolution
	glViewport(0, 0, m_lightFramebuffer->Width(), m_lightFramebuffer->Height());
	glScissor(0, 0, m_lightFramebuffer->Width(), m_lightFramebuffer->Height());

	m_lightFramebuffer->bind();

	// {
	// 	GLenum lightDraws[] = { GL_COLOR_ATTACHMENT0 };
	// 	glDrawBuffers(1, lightDraws);
	// }

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	// Copy normals and depth from the geometry buffer into the light buffer
	// We no longer blit geometry attachments into the light framebuffer every frame.
	// Light shaders sample geometry textures (normals/depth) directly from the geometry FBO to avoid
	// read-while-write hazards and reduce expensive blits.

	// Disable depth writes for light accumulation
	glDepthMask(GL_FALSE);

	// Enable additive blending explicitly for light accumulation
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);

	// Disable depth test while accumulating lights (we limit influence using normal/depth samplers in shaders)
	GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
	glDisable(GL_DEPTH_TEST);

	// lighting pass (skip loop if empty)
	if (!m_lights.empty()) {
		for (auto* light : m_lights) {
			if (light) {
				light->OnRender(m_multipliedMatrix);
			}
		}
	}

	// Global light pass: disable depth test (we own the state)
	// (we already disabled it for accumulation; keep disabled while shading global light)

	// Note: do not run a full-screen light shading pass here while the light FBO is bound.
	// We only accumulate per-light contributions into m_lightFramebuffer. The final
	// shading/combine is done in CombinedRenderPass where we can safely sample the
	// light texture (m_lightFramebuffer->GetColorTexture(0)).

	// Restore GL state to known defaults
	glBindTexture(GL_TEXTURE_2D, 0);
	if (prevDepthTest) {
		glEnable(GL_DEPTH_TEST);
	} else {
		glDisable(GL_DEPTH_TEST);
	}
	glDisable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_TRUE);

	// Unbind light framebuffer; CombinedRenderPass will read from its texture
	Framebuffer::unbind();

	// Restore viewport and scissor to output resolution
	glViewport(0, 0, m_outputFramebuffer->Width(), m_outputFramebuffer->Height());
	glScissor(0, 0, m_outputFramebuffer->Width(), m_outputFramebuffer->Height());
}

void OpenGLRenderer::CombinedRenderPass() const {
#ifdef TRACY_ENABLE
	PROFILE_ZONE;
	TracyGpuZone("Combined Pass");
#endif

	m_outputFramebuffer->bind();

	// Disable depth test for full-screen combine; we'll restore after.
	glDisable(GL_DEPTH_TEST);

	// Ensure we're drawing to the primary color attachment (attachment 0)
	glDrawBuffer(GL_COLOR_ATTACHMENT0);

	// Ensure viewport and scissor match the output framebuffer
	glViewport(0, 0, m_outputFramebuffer->Width(), m_outputFramebuffer->Height());
	glScissor(0, 0, m_outputFramebuffer->Width(), m_outputFramebuffer->Height());

	if (m_globalLightEnabled) {
		// When lighting is enabled, sample the light accumulation texture directly from the light FBO
		// bind albedo (geometry attachment 0)
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_geometryFramebuffer->GetColorTexture(0));

		// bind light accumulation texture (light FBO attachment 0)
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, m_lightFramebuffer->GetColorTexture(0));

		// draw screen quad
		m_combineLightShader->Use();
		m_combineLightShader->SetSampler("gAlbedoTexture", 0);
		m_combineLightShader->SetSampler("gLightingTexture", 1);
		// set global ambient/global light uniforms
		m_combineLightShader->Set("gGlobalLightIntensity", m_globalLightIntensity);
		m_combineLightShader->Set("gGlobalLightColor", m_globalLightColor);
		m_quad->Draw();

		// Unbind textures (keep pipeline clean)
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0);
	} else {
		// Global light disabled: skip light blits and draw pure albedo into output
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_geometryFramebuffer->GetColorTexture(0));

		m_screenShader->Use();
		m_screenShader->SetSampler("screenTexture", 0);
		m_quad->Draw();

		// Unbind texture
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	// Restore depth test state to default
	glEnable(GL_DEPTH_TEST);

	Framebuffer::unbind();
}

void OpenGLRenderer::Clear() {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGLRenderer::Resize(glm::uvec2 size) {
	unsigned width = size.x;
	unsigned height = size.y;

	m_geometryFramebuffer->Resize(width * m_config.resolutionScale, height * m_config.resolutionScale);

	m_lightFramebuffer->Resize(
	    static_cast<unsigned int>(width * m_config.lightResolutionScale), static_cast<unsigned int>(height * m_config.lightResolutionScale)
	);
	m_outputFramebuffer->Resize(width, height);
	m_layerFramebuffer->Resize(width, height);

	// Resize HUD layers
	if (m_layerStack) {
		for (auto* layer : m_layerStack->GetLayers()) {
			if (auto* hudLayer = dynamic_cast<renderer::HUD::HUDLayer*>(layer)) {
				hudLayer->Resize(width, height);
			}
		}
	}

	// Update projection matrix to maintain aspect ratio
	SetProjectionMatrix(glm::radians(90.0f), static_cast<float>(width) / static_cast<float>(height), 0.1f, 1000.0f);

	m_config.resolution = glm::uvec2(width, height);
	SaveRenderSettings();
}

void OpenGLRenderer::AddRenderable(IRenderable* renderable) {
	m_renderables.push_back(renderable);
	m_renderablesSortDirty = true;
}

void OpenGLRenderer::RemoveRenderable(IRenderable* renderable) {
	m_renderables.erase(std::ranges::find(m_renderables, renderable));
	m_renderablesSortDirty = true;
}

void OpenGLRenderer::AddTransparentRenderable(IRenderable* renderable) {
	m_transparentRenderables.push_back(renderable);
	m_transparentSortDirty = true;
}

void OpenGLRenderer::RemoveTransparentRenderable(IRenderable* renderable) {
	auto it = std::ranges::find(m_transparentRenderables, renderable);
	if (it != m_transparentRenderables.end()) {
		m_transparentRenderables.erase(it);
	}
	m_transparentSortDirty = true;
}

void OpenGLRenderer::SpritePass() {
#ifdef TRACY_ENABLE
	PROFILE_ZONE;
	TracyGpuZone("Sprite Pass");
#endif

	if (m_transparentRenderables.empty()) {
		return;
	}

	// Sort back-to-front so sprites layer correctly with alpha blending
	if (m_transparentSortDirty && m_transparentRenderables.size() > 1) {
		std::ranges::stable_sort(m_transparentRenderables, [](IRenderable* a, IRenderable* b) {
			return a->GetDepth() < b->GetDepth();
		});
		m_transparentSortDirty = false;
	}

	// Render transparent objects into the geometry framebuffer (albedo + normals attachments)
	glViewport(0, 0, m_geometryFramebuffer->Width(), m_geometryFramebuffer->Height());
	glScissor(0, 0, m_geometryFramebuffer->Width(), m_geometryFramebuffer->Height());
	m_geometryFramebuffer->bind();

	// {
	// 	GLenum draws[] = { GL_COLOR_ATTACHMENT0 };
	// 	glDrawBuffers(1, draws);
	// }

	// Enable alpha blending and depth test, but do not write to depth
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);

	for (auto* r : m_transparentRenderables) {
		if (r) {
			r->OnRender(m_multipliedMatrix);
		}
	}

	// Restore state
	glDisable(GL_BLEND);

	// {
	// 	GLenum drawsBack[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	// 	glDrawBuffers(2, drawsBack);
	// }

	Framebuffer::unbind();
}

void OpenGLRenderer::AddLight(Light2D* light) {
	m_lights.push_back(light);
	m_lightsSortDirty = true;
}

void OpenGLRenderer::RemoveLight(Light2D* light) {
	m_lights.erase(std::ranges::find(m_lights, light));
	m_lightsSortDirty = true;
}

void OpenGLRenderer::ApplyRenderSettings() {
	auto window = toast::Window::GetInstance();

	// vsync
	window->SetVSync(m_config.vSync);

	window->SetRefreshFrameTime(1000.0 / m_config.maxFPS);

	window->SetDisplayMode(m_config.currentDisplayMode);

	// resolution (Framebuffer scale is handled in Resize)
	window->SetResolution(m_config.resolution);
}

void OpenGLRenderer::HUDPass() {
	PROFILE_ZONE;
#ifdef TRACY_ENABLE
	TracyGpuZone("HUD Pass");
#endif

	if (!m_layerStack) {
		return;
	}

	// Collect all HUD layers from the layer stack
	std::vector<HUD::HUDLayer*> hudLayers;
	for (auto* layer : m_layerStack->GetLayers()) {
		if (auto* hud = dynamic_cast<HUD::HUDLayer*>(layer)) {
			if (hud->GetFramebuffer()) {
				hudLayers.push_back(hud);
			}
		}
	}

	if (hudLayers.empty()) {
		return;
	}

	// Composite each HUD framebuffer onto the output using alpha blending (over operation)
	m_outputFramebuffer->bind();
	// Ensure draw to attachment 0
	glDrawBuffer(GL_COLOR_ATTACHMENT0);

	glViewport(0, 0, m_outputFramebuffer->Width(), m_outputFramebuffer->Height());
	glScissor(0, 0, m_outputFramebuffer->Width(), m_outputFramebuffer->Height());

	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	// Ultralight renders with straight (un-premultiplied) alpha
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	m_screenShader->Use();

	for (auto* hud : hudLayers) {
		GLuint hudTex = hud->GetFramebuffer()->GetColorTexture(0);
		if (hudTex == 0) {
			continue;
		}

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, hudTex);
		m_screenShader->SetSampler("screenTexture", 0);

		m_quad->Draw();
	}

	// Restore state
	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);

	Framebuffer::unbind();
}

}
