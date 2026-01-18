#pragma once
#include <cstdint>
#include <glm/glm.hpp>

namespace renderer {

class ParticleEmitter {
public:
	explicit ParticleEmitter(uint32_t maxParticles = 65536);
	~ParticleEmitter();

	uint32_t GetId() const;

	void Start();
	void Stop();
	void Update(float dt);

	// Configure spawn rate (particles per second)
	void SetRate(float rate) {
		m_rate = rate;
	}

	float GetRate() const {
		return m_rate;
	}

	// Request an immediate burst
	void EmitBurst(uint32_t count) {
		m_pendingBurst += count;
	}

	void TriggerBurst() {
		if (burstCount > 0 && !burstTriggered) {
			EmitBurst(burstCount);
			burstTriggered = true;
			if (!loop) {
				m_enabled = false;
			}
		}
	}

	// Called by ParticleSystemManager to get how many particles to spawn this frame.
	uint32_t ConsumeSpawnRequests();

	//@TODO: Change
	glm::vec3 position { 0.0f };
	glm::vec3 startVelocity { 0.0f, 0.5f, 0.0f };
	float startLife = 3.0f;
	float startSize = 0.15f;
	glm::vec4 startColor { 0.8f, 0.8f, 0.8f, 0.6f };

	// End-state for interpolation over lifetime
	float endSize = 1.0f;
	glm::vec4 endColor { 0.2f, 0.2f, 0.2f, 0.0f };

	// Burst/loop control
	bool loop = true;           // if false, emitter will do a one-shot burst and stop
	uint32_t burstCount = 0;    // number of particles to emit when triggered

private:
	uint32_t m_id = 0;
	uint32_t m_maxParticles = 0;
	bool m_enabled = false;

	// spawning
	float m_rate = 50.0f;    // particles per second
	float m_accumulator = 0.0f;
	uint32_t m_pendingBurst = 0;

	bool burstTriggered = false;

	static uint32_t s_nextId;
};

}    // namespace renderer
