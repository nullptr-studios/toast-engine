#pragma once
#include "Framebuffer.hpp"
#include "IPostProcess.hpp"
#include "glm/vec2.hpp"

#include <memory>
#include <vector>

class PostProcessManager {
public:
	PostProcessManager();
	~PostProcessManager();

	static PostProcessManager* Get() {
		return instance;
	}

	void PostProcessPass(Framebuffer* inputFBO, Framebuffer* outputFBO);

	void AddGlobalProcess(std::unique_ptr<IPostProcess> effect);
	void ClearGlobalProcesses();

	void SetOverrideStack(std::vector<IPostProcess*>* stack, int priority);
	void ClearOverride();

	void GlobalInspector();

	void InitBuffers(const glm::ivec2& resolution);

private:
	static PostProcessManager* instance;

	// Ping-pong buffers
	Framebuffer* m_fboA = nullptr;
	Framebuffer* m_fboB = nullptr;
	GLuint m_texA = 0;
	GLuint m_texB = 0;

	int m_width = 0;
	int m_height = 0;

	// Global effects
	std::vector<std::unique_ptr<IPostProcess>> m_globalStack;

	// Override
	std::vector<IPostProcess*>* m_overrideStack = nullptr;
	int m_overridePriority = -1;

private:
	GLuint ExecuteStack(std::vector<IPostProcess*>* stack, Framebuffer* inputFBO) const;
};
