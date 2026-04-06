#pragma once
#include "Framebuffer.hpp"
#include "IPostProcess.hpp"
#include "glm/vec2.hpp"

#include <memory>
#include <string>
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
	bool RemoveGlobalProcess(size_t index);
	bool MoveGlobalProcess(size_t from, size_t to);
	[[nodiscard]]
	std::vector<std::unique_ptr<IPostProcess>>& GetGlobalStack() {
		return m_globalStack;
	}
	[[nodiscard]]
	const std::vector<std::unique_ptr<IPostProcess>>& GetGlobalStack() const {
		return m_globalStack;
	}
	void ClearGlobalProcesses();
	bool AddGlobalProcessByType(const std::string& typeId);

	void SetOverrideStack(std::vector<IPostProcess*>* stack, int priority);
	void ClearOverride();

	[[nodiscard]]
	json_t SaveStack(const std::vector<IPostProcess*>& stack) const;
	[[nodiscard]]
	json_t SaveStack(const std::vector<std::unique_ptr<IPostProcess>>& stack) const;
	[[nodiscard]]
	json_t SaveGlobalStack() const;

	bool LoadGlobalStack(const json_t& j);
	bool LoadStackInto(const json_t& j, std::vector<std::unique_ptr<IPostProcess>>& dst);
	[[nodiscard]]
	bool LoadGlobalFromConfigInto(
	    std::vector<std::unique_ptr<IPostProcess>>& dst,
	    const std::string& path = "assets/Renderer.postprocess.settings"
	);

	[[nodiscard]]
	bool SaveGlobalToConfig(const std::string& path = "assets/Renderer.postprocess.settings") const;
	[[nodiscard]]
	bool LoadGlobalFromConfig(const std::string& path = "assets/Renderer.postprocess.settings");

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
	std::unique_ptr<IPostProcess> m_mandatoryTonemapping;

	// Override
	std::vector<IPostProcess*>* m_overrideStack = nullptr;
	int m_overridePriority = -1;

private:
	GLuint ExecuteStack(std::vector<IPostProcess*>* stack, Framebuffer* inputFBO);
};
