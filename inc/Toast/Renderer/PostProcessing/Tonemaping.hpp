/// @file Tonemaping.hpp
/// @author dario
/// @date 24/03/2026.


#pragma once
#include "Toast/Renderer/IPostProcess.hpp"
#include "Toast/Renderer/Shader.hpp"

#include <memory>

struct Tonemaping : public IPostProcess {
	Tonemaping();
	
	
	void Execute(unsigned int inTex, unsigned int outFbo) override;
	
private:
	std::shared_ptr<renderer::Shader> m_tonemapShader;
};
