#pragma once

#include <cstdint>
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <list>
#include <memory>
#include <vector>

namespace renderer {
class ParticleEmitter;
class Mesh;
class Shader;

class ParticleSystemManager {
public:
	ParticleSystemManager();
	~ParticleSystemManager();

	void OnAttach();
	void OnDetach();

	void Tick(float dt);
	void Render(const glm::mat4& viewProj);

	// Emitter management
	ParticleEmitter* CreateEmitter(uint32_t maxParticles = 65536);
	void DestroyEmitter(ParticleEmitter* emitter);

	// Runtime settings
	void SetGravity(const glm::vec3& g) {
		m_gravity = g;
	}

	void SetMaxParticles(uint32_t max) {
		m_maxParticles = max;
	}

	[[nodiscard]]
	uint32_t GetMaxParticles() const {
		return m_maxParticles;
	}

	// Readback interval control
	void SetReadbackInterval(int v) {
		m_readbackInterval = std::max(1, v);
	}

	[[nodiscard]]
	int GetReadbackInterval() const {
		return m_readbackInterval;
	}

	[[nodiscard]]
	glm::vec3 GetGravity() const {
		return m_gravity;
	}

	// Expose emitters
	[[nodiscard]]
	const std::list<ParticleEmitter*>& GetEmitters() const {
		return m_emitters;
	}

private:
	// GPU resource handles
	GLuint m_ssboA = 0;
	GLuint m_ssboB = 0;
	GLuint m_counters = 0;
	GLuint m_emitRequests = 0;
	GLuint m_uboFrameParams = 0;

	// active buffer pointers
	GLuint m_ssboIn = 0;
	GLuint m_ssboOut = 0;

	std::shared_ptr<Shader> m_computeShader;
	std::shared_ptr<Shader> m_renderShader;

	std::shared_ptr<Mesh> m_quadMesh;

	uint32_t m_maxParticles = 65536;
	uint32_t m_inCount = 0;    // tracked on CPU

	std::list<ParticleEmitter*> m_emitters;

	// reduce glGetNamedBufferSubData frequency
	int m_readbackInterval = 5;    // frames
	int m_readbackCounter = 0;

	glm::vec3 m_gravity { 0.0f, -9.81f, 0.0f };

	// last frame delta
	float m_lastDt = 0.0f;

	void SwapBuffers();
};

}    // namespace renderer
