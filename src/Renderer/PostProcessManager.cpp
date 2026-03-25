/// @file PostProcessManager.cpp
/// @author dario
/// @date 23/03/2026.

#include "Toast/Renderer/PostProcessManager.hpp"

#include "Toast/Log.hpp"
#include "imgui.h"

PostProcessManager* PostProcessManager::instance = nullptr;

GLuint PostProcessManager::ExecuteStack(std::vector<IPostProcess*>* stack, Framebuffer* inputFBO) const {
	Framebuffer* srcFBO = inputFBO;
	Framebuffer* dstFBO = m_fboA;

	for (auto* effect : *stack) {
		effect->Execute(srcFBO, dstFBO);

		srcFBO = dstFBO;
		dstFBO = (dstFBO == m_fboA) ? m_fboB : m_fboA;
	}

	return srcFBO->GetColorTexture(0);
}

PostProcessManager::PostProcessManager() {
	if (instance) {
		TOAST_ERROR("Multiple instances of PostProcessManager, This should never happen.");
	}
	instance = this;
}

PostProcessManager::~PostProcessManager() {
	delete m_fboA;
	delete m_fboB;
}

void PostProcessManager::PostProcessPass(Framebuffer* inputFBO, Framebuffer* outputFBO) {
	if (!m_fboA || !m_fboB) {
		TOAST_WARN("PostProcessManager buffers not initialized, falling back to direct blit");
		inputFBO->BlitTo(outputFBO, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		return;
	}

	GLuint finalTex = 0;
	if (m_overrideStack) {
		if (m_overrideStack->empty()) {
			finalTex = inputFBO->GetColorTexture(0);
		} else {
			finalTex = ExecuteStack(m_overrideStack, inputFBO);
		}
	} else {
		std::vector<IPostProcess*> temp;
		temp.reserve(m_globalStack.size());
		for (auto& e : m_globalStack) {
			temp.push_back(e.get());
		}

		if (temp.empty()) {
			finalTex = inputFBO->GetColorTexture(0);
		} else {
			finalTex = ExecuteStack(&temp, inputFBO);
		}
	}

	// blit the final texture into output FB
	Framebuffer* srcFBO = nullptr;

	if (finalTex == inputFBO->GetColorTexture(0)) {
		srcFBO = inputFBO;
	} else if (finalTex == m_texA) {
		srcFBO = m_fboA;
	} else if (finalTex == m_texB) {
		srcFBO = m_fboB;
	} else {
		TOAST_WARN("PostProcessManager: final texture not recognized, falling back to inputFBO blit");
		inputFBO->BlitTo(outputFBO, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		return;
	}

	glDisable(GL_DEPTH_TEST);

	outputFBO->bind();

	glDrawBuffer(GL_COLOR_ATTACHMENT0);

	glViewport(0, 0, outputFBO->Width(), outputFBO->Height());
	glScissor(0, 0, outputFBO->Width(), outputFBO->Height());

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	srcFBO->BlitTo(outputFBO, GL_COLOR_BUFFER_BIT, GL_LINEAR);

	glEnable(GL_DEPTH_TEST);

	Framebuffer::unbind();
}

void PostProcessManager::AddGlobalProcess(std::unique_ptr<IPostProcess> effect) {
	m_globalStack.push_back(std::move(effect));
}

void PostProcessManager::ClearGlobalProcesses() {
	m_globalStack.clear();
}

void PostProcessManager::SetOverrideStack(std::vector<IPostProcess*>* stack, int priority) {
	if (priority >= m_overridePriority) {
		m_overrideStack = stack;
		m_overridePriority = priority;
	}
}

void PostProcessManager::ClearOverride() {
	m_overrideStack = nullptr;
	m_overridePriority = -1;
}

void PostProcessManager::GlobalInspector() {
	if (ImGui::CollapsingHeader("Global Post-Processing Effects")) {
		for (auto& effect : m_globalStack) {
			if (effect) {
				effect->Inspector();
			}
		}
	}
}

void PostProcessManager::InitBuffers(const glm::ivec2& resolution) {
	Framebuffer::Specs specs = { resolution.x, resolution.y };
	if (!m_fboA) {
		m_fboA = new Framebuffer(specs);
		m_fboA->AddColorAttachment(GL_RGBA16F, GL_RGBA, GL_FLOAT);
		m_fboA->Build();
	} else {
		m_fboA->Resize(resolution.x, resolution.y);
	}

	if (!m_fboB) {
		m_fboB = new Framebuffer(specs);
		m_fboB->AddColorAttachment(GL_RGBA16F, GL_RGBA, GL_FLOAT);
		m_fboB->Build();
	} else {
		m_fboB->Resize(resolution.x, resolution.y);
	}

	m_texA = m_fboA->GetColorTexture(0);
	m_texB = m_fboB->GetColorTexture(0);
}
