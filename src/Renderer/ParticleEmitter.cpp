#include "Toast/Renderer/ParticleEmitter.hpp"

namespace renderer {

uint32_t ParticleEmitter::s_nextId = 1;

ParticleEmitter::ParticleEmitter(uint32_t maxParticles) : m_maxParticles(maxParticles) {
	m_id = s_nextId++;
}

ParticleEmitter::~ParticleEmitter() = default;

uint32_t ParticleEmitter::GetId() const {
	return m_id;
}

void ParticleEmitter::Start() {
	m_enabled = true;
	// If configured as a one-shot burst and not previously triggered, queue the burst immediately
	if (!loop && burstCount > 0 && !burstTriggered) {
		m_pendingBurst += burstCount;
		burstTriggered = true;
		// If not looping, disable once the burst has been consumed
	}
}

void ParticleEmitter::Stop() {
	m_enabled = false;
}

void ParticleEmitter::Update(float dt) {
	if (!m_enabled) {
		return;
	}
	// accumulate particles to spawn based on rate for continuous emitters
	if (m_rate > 0.0f && loop) {
		m_accumulator += m_rate * dt;
		uint32_t toSpawn = static_cast<uint32_t>(m_accumulator);
		if (toSpawn > 0) {
			m_pendingBurst += toSpawn;
			m_accumulator -= static_cast<float>(toSpawn);
		}
	}
}

uint32_t ParticleEmitter::ConsumeSpawnRequests() {
	uint32_t out = m_pendingBurst;
	m_pendingBurst = 0;
	// If this was a non-looping burst emitter, stop after the burst was handed off
	if (!loop && burstTriggered) {
		m_enabled = false;
	}
	return out;
}

}    // namespace renderer
