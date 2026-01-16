//
// Created by dario on 14/09/2025.
//

#include "Toast/Renderer/OpenGL/OpenGLRenderer.hpp"

#include "Toast/GlmJson.hpp"
#include "Toast/Log.hpp"
#include "Toast/Profiler.hpp"
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

#include "Toast/Renderer/OclussionVolume.hpp"

#include <stb_image.h>

#ifdef TRACY_ENABLE
#include <tracy/TracyOpenGL.hpp>
#endif

namespace renderer {

IRendererBase* IRendererBase::m_instance = nullptr;

#ifdef TOAST_EDITOR
static GLFWwindow* g_backup_current_context = nullptr;
#endif

#ifndef _NDEBUG
void DebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* user_param) {
	// ignore non-significant error/warning codes
	if (id == 131169 || id == 131185 || id == 131218 || id == 131204) {
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
#ifndef _NDEBUG
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
	// m_lightFramebuffer->AddDepthAttachment();
	m_lightFramebuffer->Build();

	m_outputFramebuffer = new Framebuffer(s);
	m_outputFramebuffer->AddColorAttachment(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);    // final output buffer
	m_outputFramebuffer->AddColorAttachment(GL_RGBA16F, GL_RGBA, GL_FLOAT);          // lighting info
	m_outputFramebuffer->Build();

	//@TODO: HDR support

	// Listen to window resize events
	m_listener.Subscribe<event::WindowResize>([this](event::WindowResize* e) -> bool {
		if (e->width == 0 || e->height == 0) {
			return true;
		}
		// Update viewport and projection matrix
		Resize(e->width, e->height);
		return true;
	});

	// call resize once at start
	{
		auto* window = toast::Window::GetInstance();
		OpenGLRenderer::Resize(window->GetFramebufferSize().first, window->GetFramebufferSize().second);
	}

#ifdef TOAST_EDITOR
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;    // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;     // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;        // IF using Docking Branch
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;      // Enable Multi-Viewport

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForOpenGL(
	    toast::Window::GetInstance()->GetWindow(), true
	);    // Second param install_callback=true will install GLFW callbacks and chain to existing ones.
	ImGui_ImplOpenGL3_Init();

	g_backup_current_context = glfwGetCurrentContext();
#endif

	// setup layerstack
	m_layerStack = LayerStack::GetInstance();

	// setup default resources
	m_quad = resource::ResourceManager::GetInstance()->LoadResource<renderer::Mesh>("models/quad.obj");
	m_screenShader = resource::ResourceManager::GetInstance()->LoadResource<renderer::Shader>("shaders/screen.shader");
	m_combineLightShader = resource::ResourceManager::GetInstance()->LoadResource<renderer::Shader>("shaders/combineLight.shader");
	m_globalLightShader = resource::ResourceManager::GetInstance()->LoadResource<renderer::Shader>("shaders/globalLight.shader");

	// Set once, change and reset state if needed
	stbi_set_flip_vertically_on_load(1);
}

OpenGLRenderer::~OpenGLRenderer() {
	TOAST_INFO("Shutting down OpenGL Renderer...");
	delete m_geometryFramebuffer;
	delete m_lightFramebuffer;
	delete m_outputFramebuffer;
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

	// Geometry
	GeometryPass();

	// Lighting
	LightingPass();

	// Combine
	CombinedRenderPass();

	// Render editor/game layers into output buffer
	m_outputFramebuffer->bind();
	m_layerStack->RenderLayers();
	Framebuffer::unbind();

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
	TracyGpuZone("Geometry Pass");
#endif

	// Sort by depth (front-to-back) only when necessary.
	if (m_renderables.size() > 1) {
		std::ranges::stable_sort(m_renderables, [](IRenderable* a, IRenderable* b) {
			return a->GetDepth() < b->GetDepth();
		});
	}

	m_geometryFramebuffer->bind();
	Clear();

	// geometry pass
	for (auto* r : m_renderables) {
		if (r) {
			r->OnRender(m_multipliedMatrix);
		}
	}
	// Don't unbind here - will be unbound when next framebuffer is bound
}

void OpenGLRenderer::LightingPass() {
#ifdef TRACY_ENABLE
	TracyGpuZone("Lighting Pass");
#endif

	// If global lighting is disabled, avoid all light buffer work
	if (!m_globalLightEnabled) {
		return;
	}

	// Sort lights by z to ensure correct accumulation ordering when needed
	if (m_lights.size() > 1) {
		std::ranges::stable_sort(m_lights, [](Light2D* a, Light2D* b) {
			return a->transform()->position().z < b->transform()->position().z;
		});
	}

	// Render lights at their own resolution
	glViewport(0, 0, m_lightFramebuffer->Width(), m_lightFramebuffer->Height());
	glScissor(0, 0, m_lightFramebuffer->Width(), m_lightFramebuffer->Height());

	m_lightFramebuffer->bind();
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	// Copy normals and depth from the geometry buffer into the light buffer
	m_geometryFramebuffer->BlitTo(m_lightFramebuffer, GL_COLOR_BUFFER_BIT, GL_LINEAR, 1, 1);    // copy normals buffer
	// m_geometryFramebuffer->BlitTo(m_lightFramebuffer, GL_DEPTH_BUFFER_BIT, GL_NEAREST);         // copy depth buffer

	// Disable depth writes for light accumulation
	glDepthMask(GL_FALSE);

	// Use additive blending for light accumulation (restore after pass)
	glBlendFunc(GL_ONE, GL_ONE);

	// lighting pass (skip loop if empty)
	if (!m_lights.empty()) {
		for (auto* light : m_lights) {
			if (light) {
				light->OnRender(m_multipliedMatrix);
			}
		}
	}

	// Global light pass: disable depth test (we own the state)
	glDisable(GL_DEPTH_TEST);

	m_globalLightShader->Use();
	m_globalLightShader->Set("gLightIntensity", m_globalLightIntensity);
	m_globalLightShader->Set("gLightColor", m_globalLightColor);

	// bind light accumulation texture
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_lightFramebuffer->GetColorTexture(0));
	m_globalLightShader->SetSampler("gLightAccumulationTex", 0);

	// draw full screen quad
	m_quad->Draw();

	// Restore GL state to known defaults
	glBindTexture(GL_TEXTURE_2D, 0);
	glEnable(GL_DEPTH_TEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_TRUE);

	// Restore viewport and scissor to output resolution
	glViewport(0, 0, m_outputFramebuffer->Width(), m_outputFramebuffer->Height());
	glScissor(0, 0, m_outputFramebuffer->Width(), m_outputFramebuffer->Height());
}

void OpenGLRenderer::CombinedRenderPass() const {
#ifdef TRACY_ENABLE
	TracyGpuZone("Combined Pass");
#endif

	m_outputFramebuffer->bind();

	// Disable depth test for full-screen combine; we'll restore after.
	glDisable(GL_DEPTH_TEST);

	if (m_globalLightEnabled) {
		// When lighting is enabled, blit lighting buffer and combine
		m_lightFramebuffer->BlitTo(m_outputFramebuffer, GL_COLOR_BUFFER_BIT, GL_LINEAR, 0, 1);

		// bind albedo
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_geometryFramebuffer->GetColorTexture(0));

		// bind light accumulation texture
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, m_outputFramebuffer->GetColorTexture(1));

		// draw screen quad
		m_combineLightShader->Use();
		m_combineLightShader->SetSampler("gAlbedoTexture", 0);
		m_combineLightShader->SetSampler("gLightingTexture", 1);
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

	// Restore depth test state to default (enabled)
	glEnable(GL_DEPTH_TEST);

	Framebuffer::unbind();
}

void OpenGLRenderer::Clear() {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGLRenderer::Resize(unsigned int width, unsigned int height) {
	glViewport(0, 0, width, height);
	glScissor(0, 0, width, height);
	m_geometryFramebuffer->Resize(width, height);

	m_lightFramebuffer->Resize(static_cast<unsigned int>(width * m_globalLightResolution), static_cast<unsigned int>(height * m_globalLightResolution));
	m_outputFramebuffer->Resize(width, height);
	// Update projection matrix to maintain aspect ratio
	SetProjectionMatrix(glm::radians(90.0f), static_cast<float>(width) / static_cast<float>(height), 0.1f, 1000.0f);
}

void OpenGLRenderer::AddRenderable(IRenderable* renderable) {
	m_renderables.push_back(renderable);
}

void OpenGLRenderer::RemoveRenderable(IRenderable* renderable) {
	m_renderables.erase(std::ranges::find(m_renderables, renderable));
}

void OpenGLRenderer::AddLight(Light2D* light) {
	m_lights.push_back(light);
}

void OpenGLRenderer::RemoveLight(Light2D* light) {
	m_lights.erase(std::ranges::find(m_lights, light));
}

void OpenGLRenderer::LoadRenderSettings() {
	json_t settings {};
	std::istringstream ss;
	if (!resource::ResourceManager::GetInstance()->OpenFile("renderer_settings.toast", ss)) {
		throw ToastException("Failed to find project_settings.toast");
	}

	ss >> settings;

	m_globalLightEnabled = settings["GlobalIllumination"]["enabled"].get<bool>();
	m_globalLightIntensity = settings["GlobalIllumination"]["intensity"].get<float>();
	m_globalLightColor = settings["GlobalIllumination"]["color"].get<glm::vec3>();
	m_globalLightResolution = settings["GlobalIllumination"]["resolution"].get<float>();
}

void OpenGLRenderer::SaveRenderSettings() {
	json_t settings {};
	settings["GlobalIllumination"]["enabled"] = m_globalLightEnabled;
	settings["GlobalIllumination"]["intensity"] = m_globalLightIntensity;
	settings["GlobalIllumination"]["color"] = m_globalLightColor;
	settings["GlobalIllumination"]["resolution"] = m_globalLightResolution;

	resource::ResourceManager::SaveFile("renderer_settings.toast", settings.dump(2));
}

}
