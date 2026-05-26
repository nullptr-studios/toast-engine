/// @file OpenGLRenderer.hpp
/// @author Dario
/// @date 14/09/25
#pragma once

#include "Toast/Renderer/Framebuffer.hpp"
#include "Toast/Renderer/HUD/HUDActor.hpp"
#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Renderer/Lights/GlobalLight.hpp"
#include "Toast/Renderer/PostProcessManager.hpp"
#include "Toast/Renderer/PostProcessVolume.hpp"

namespace renderer {
class LayerStack;
class Shader;
class Mesh;
namespace HUD {
class HUDLayer;
}
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
	void DirectionalShadowPass();
	void PostProcessPass();
	void LightingPass();
	void CombinedRenderPass() const;
	void SpritePass();
	void WaterPass();
	void HUDPass(const std::vector<HUD::HUDLayer*>& hudLayers);

	void Clear() const override;
	void Resize(glm::uvec2 size) override;

	void AddRenderable(IRenderable* renderable) override;
	void RemoveRenderable(IRenderable* renderable) override;
	
	void AddTransparent(IRenderable* renderable) override;
	void RemoveTransparent(IRenderable* renderable) override;

	void AddWater(IRenderable* renderable) override;
	void RemoveWater(IRenderable* renderable) override;

	void AddLight(Light2D* light) override;
	void RemoveLight(Light2D* light) override;

	void ApplyRenderSettings() override;

	void DrawScreenQuad(bool flipY, bool useShader) override;
	void InvalidateGLStateCaches() override;

	GLuint GetShadowMapTexture() const override;

private:
	void RecreateShadowResources(unsigned resolution);
	void DestroyShadowResources();
	void RecreateDirectionalShadowResources(unsigned resolution);
	void DestroyDirectionalShadowResources();
	void UpdateDirectionalShadowMatrix();
	void CreateOrResizeWaterSceneCopyTexture(int width, int height);
	void DestroyWaterSceneCopyTexture();

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
		std::vector<IRenderable*> combinedTransparents;
		std::vector<IRenderable*> combinedWaters;
		Framebuffer* postProcessFramebuffer = nullptr;
		GLuint waterSceneCopyTexture = 0;
		int waterSceneCopyWidth = 0;
		int waterSceneCopyHeight = 0;

		// Shadows
		Framebuffer* occlusionFramebuffer = nullptr;
		GLuint jfaTex = 0;
		GLuint pingPongTex = 0;
		GLuint sdfTex = 0;
		std::shared_ptr<Shader> sdfComputeShader = nullptr;
		std::shared_ptr<Shader> jfaInitComputeShader = nullptr;
		std::shared_ptr<Shader> jfaComputeShader = nullptr;
		std::shared_ptr<Shader> finalComputeShader = nullptr;

		Framebuffer* directionalShadowFramebuffer = nullptr;
		glm::vec3 lastDirectionalLightDir = glm::vec3(0.0f);
		glm::vec3 lastDirectionalCameraPos = glm::vec3(0.0f);
		glm::mat4 lastDirectionalViewProj = glm::mat4(1.0f);
		bool directionalShadowMatrixDirty = true;
		
		PostProcessVolume* amongas;
		HUDActor* amongas2;
		// Framebuffer* layerFramebuffer = nullptr;
	} m;
};
}
