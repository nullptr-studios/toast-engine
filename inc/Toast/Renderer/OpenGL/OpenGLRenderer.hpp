/// @file OpenGLRenderer.hpp
/// @author Dario
/// @date 14/09/25
#pragma once

#include "Toast/Renderer/Framebuffer.hpp"
#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Renderer/Lights/GlobalLight.hpp"

namespace renderer {
class LayerStack;
class Shader;
class Mesh;
}

namespace renderer {
///@class OpenGLRenderer
///@brief OpenGL implementation
class OpenGLRenderer : public renderer::IRendererBase {
public:
	OpenGLRenderer();
	~OpenGLRenderer() override;

	void StartImGuiFrame() override;
	void EndImGuiFrame() override;

	void Render() override;

	void GeometryPass();
	void OcclusionPass();
	void LightingPass();
	void CombinedRenderPass() const;
	void SpritePass();
	void HUDPass();

	void Clear() const override;
	void Resize(glm::uvec2 size) override;

	void AddRenderable(IRenderable* renderable) override;
	void RemoveRenderable(IRenderable* renderable) override;

	void AddLight(Light2D* light) override;
	void RemoveLight(Light2D* light) override;

	void ApplyRenderSettings() override;

	void DrawScreenQuad(bool flipY) override;

	GLuint GetShadowMapTexture() const override;

private:
	void RecreateShadowResources(unsigned resolution);
	void DestroyShadowResources();

	struct {
		LayerStack* layerStack = nullptr;

		// Rendering resources
		std::shared_ptr<Shader> screenShader = nullptr;
		std::shared_ptr<Shader> flippedScreenShader = nullptr;
		std::shared_ptr<Shader> combineLightShader = nullptr;
		std::shared_ptr<Shader> globalLightShader = nullptr;
		std::shared_ptr<Mesh> quad = nullptr;

		GlobalLight* globalLight = nullptr;

		std::vector<IRenderable*> combinedRenderables;
		Framebuffer* geometryResolveFramebuffer = nullptr;

		// Shadows
		Framebuffer* occlusionFramebuffer = nullptr;
		GLuint jfaTex = 0;
		GLuint pingPongTex = 0;
		GLuint sdfTex = 0;
		std::shared_ptr<Shader> sdfComputeShader = nullptr;
		std::shared_ptr<Shader> jfaInitComputeShader = nullptr;
		std::shared_ptr<Shader> jfaComputeShader = nullptr;
		std::shared_ptr<Shader> finalComputeShader = nullptr;

		// Framebuffer* layerFramebuffer = nullptr;
	} m;
};
}
