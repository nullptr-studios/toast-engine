/// @file PostProcessManager.cpp
/// @author dario
/// @date 23/03/2026.

#include "PostProcessManager.hpp"

#include "Toast/Log.hpp"

PostProcessManager* PostProcessManager::instance = nullptr;

GLuint PostProcessManager::ExecuteStack(std::vector<IPostProcess*>* stack, GLuint inputTex) {
	GLuint readTex = inputTex;
	GLuint writeFBO = m_fboA->Handle();
	GLuint writeTex = m_texA;

	for (const auto* effect : stack) {

		effect->Execute(readTex, writeFBO);

		// swap
		readTex = writeTex;

		if (writeFBO == m_fboA->Handle()) {
			writeFBO = m_fboB->Handle();
			writeTex = m_texB;
		} else {
			writeFBO = m_fboA->Handle();
			writeTex = m_texA;
		}
	}

	return readTex;
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

void PostProcessManager::PostProcessPass(GLuint inputHDRTexture) {
	if (m_overrideStack) {
		ExecuteStack(m_overrideStack, inputHDRTexture);
	} else {
		// convert unique_ptr → raw pointer list
		std::vector<IPostProcess*> temp;

		for (auto& e : m_globalStack)
			temp.push_back(e.get());

		ExecuteStack(&temp, inputHDRTexture);
	}
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

void PostProcessManager::InitBuffers(const glm::ivec2& resolution) {
	Framebuffer::Specs specs = { resolution.x, resolution.y };
	if (!m_fboA) {
		m_fboA = new Framebuffer(specs);
		m_fboA->AddColorAttachment(GL_RGBA16F, GL_RGBA, GL_FLOAT);
		m_fboA->Build();
	}
	else {
		m_fboA->Resize(resolution.x, resolution.y);
	}
	
	if (!m_fboB) {
		m_fboB = new Framebuffer(specs);
		m_fboB->AddColorAttachment(GL_RGBA16F, GL_RGBA, GL_FLOAT);
		m_fboB->Build();
	}else {
		m_fboB->Resize(resolution.x, resolution.y);
	}
	
	m_texA = m_fboA->GetColorTexture(0);
	m_texB = m_fboB->GetColorTexture(0);

}