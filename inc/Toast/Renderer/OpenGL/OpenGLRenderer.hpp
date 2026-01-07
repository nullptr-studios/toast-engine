/// @file OpenGLRenderer.hpp
/// @author Dario
/// @date 14/09/25
#pragma once

#include "Toast/Renderer/Framebuffer.hpp"

#include <Toast/Renderer/IRendererBase.hpp>
#include <Toast/Renderer/Lights/GlobalLight.hpp>

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
	void LightingPass();
	void CombinedRenderPass() const;

	void Clear() override;
	void Resize(unsigned int width, unsigned int height) override;

	void AddRenderable(IRenderable* renderable) override;
	void RemoveRenderable(IRenderable* renderable) override;

	void AddLight(Light2D* light) override;
	void RemoveLight(Light2D* light) override;

	void LoadRenderSettings() override;
	void SaveRenderSettings() override;

private:
	LayerStack* m_layerStack = nullptr;

	// Rendering resources
	std::shared_ptr<Shader> m_screenShader = nullptr;
	std::shared_ptr<Shader> m_combineLightShader = nullptr;
	std::shared_ptr<Shader> m_globalLightShader = nullptr;
	std::shared_ptr<Mesh> m_quad = nullptr;

	GlobalLight* m_globalLight = nullptr;
};
}
