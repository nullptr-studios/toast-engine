/// @file Tonemaping.cpp
/// @author dario
/// @date 24/03/2026.


#include "Tonemaping.hpp"

#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Resources/ResourceManager.hpp"

Tonemaping::Tonemaping() {
	m_tonemapShader = resource::LoadResource<renderer::Shader>("SHADERS/tonemapping.shader");
}

void Tonemaping::Execute(unsigned int inTex, unsigned int outFbo) {
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, outFbo);
	
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, inTex);
	
	m_tonemapShader->Use();
	
	renderer::IRendererBase::GetInstance()->DrawScreenQuad(false);
}
