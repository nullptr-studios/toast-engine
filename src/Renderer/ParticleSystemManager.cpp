#include "Toast/Renderer/ParticleSystemManager.hpp"

#include "Toast/Log.hpp"
#include "Toast/Renderer/ParticleEmitter.hpp"
#include "Toast/Renderer/Shader.hpp"
#include "Toast/Resources/Mesh.hpp"
#include "Toast/Resources/ResourceManager.hpp"

#include <algorithm>
#include <array>
#include <glad/gl.h>
#include <vector>

namespace renderer {

// Particle layout 6 vec4 (pos, vel, startColor, endColor+endSize(.a), curColor, misc)
static constexpr size_t PARTICLE_SIZE = 96;    // 6 * 16 bytes

ParticleSystemManager::ParticleSystemManager() {
	TOAST_INFO("ParticleSystemManager created");
}

ParticleSystemManager::~ParticleSystemManager() {
	TOAST_INFO("ParticleSystemManager destroyed");
}

std::shared_ptr<Texture> DefaultTexture;

void ParticleSystemManager::OnAttach() {
	// Compute shader
	m_computeShader = resource::ResourceManager::GetInstance()->LoadResource<renderer::Shader>("shaders/particles_compute.shader");

	// Render shader
	m_renderShader = resource::ResourceManager::GetInstance()->LoadResource<renderer::Shader>("shaders/particles_render.shader");

	// Load quad mesh
	m_quadMesh = resource::ResourceManager::GetInstance()->LoadResource<renderer::Mesh>("models/quad.obj");

	// Allocate SSBOs for max particles
	const GLsizeiptr bufSize = static_cast<GLsizeiptr>(m_maxParticles) * PARTICLE_SIZE;    // bytes per particle

	glCreateBuffers(1, &m_ssboA);
	glNamedBufferStorage(m_ssboA, bufSize, nullptr, GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT);

	glCreateBuffers(1, &m_ssboB);
	glNamedBufferStorage(m_ssboB, bufSize, nullptr, GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT);

	// counters: inCount, outCount, spawnCount, pad
	std::array<uint32_t, 4> countersInit = { 0u, 0u, 0u, 0u };
	glCreateBuffers(1, &m_counters);
	glNamedBufferStorage(m_counters, static_cast<GLsizeiptr>(sizeof(countersInit)), countersInit.data(), GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT);

	// emitRequests: small buffer for emit counts
	glCreateBuffers(1, &m_emitRequests);
	glNamedBufferStorage(m_emitRequests, sizeof(uint32_t) * PARTICLE_SIZE * 2, nullptr, GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT);

	// FrameParams UBO (std140 binding = 4)
	glCreateBuffers(1, &m_uboFrameParams);
	glNamedBufferStorage(m_uboFrameParams, static_cast<GLsizeiptr>(sizeof(float) * 8), nullptr, GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT);
	glBindBufferBase(GL_UNIFORM_BUFFER, 4, m_uboFrameParams);

	// If shader supports uniform block binding helper, set it for render & compute shaders
	m_computeShader->LoadMainThread();
	m_computeShader->SetUniformBlockBinding("FrameParams", 4);
	
	m_renderShader->LoadMainThread();
	m_renderShader->SetUniformBlockBinding("FrameParams", 4);

	// Set initial active buffers
	m_ssboIn = m_ssboA;
	m_ssboOut = m_ssboB;


	DefaultTexture = resource::ResourceManager::GetInstance()->LoadResource<Texture>("d");    // force placeholder texture
}

void ParticleSystemManager::OnDetach() {
	if (m_ssboA) {
		glDeleteBuffers(1, &m_ssboA);
		m_ssboA = 0;
	}
	if (m_ssboB) {
		glDeleteBuffers(1, &m_ssboB);
		m_ssboB = 0;
	}
	if (m_counters) {
		glDeleteBuffers(1, &m_counters);
		m_counters = 0;
	}
	if (m_emitRequests) {
		glDeleteBuffers(1, &m_emitRequests);
		m_emitRequests = 0;
	}
	if (m_uboFrameParams) {
		glDeleteBuffers(1, &m_uboFrameParams);
		m_uboFrameParams = 0;
	}

	m_computeShader.reset();
	m_renderShader.reset();
	m_quadMesh.reset();
}

void ParticleSystemManager::SwapBuffers() {
	std::swap(m_ssboIn, m_ssboOut);
	// reset outCount to zero in counters buffer for next frame
	std::array<uint32_t, 4> zeros = { 0u, 0u, 0u, 0u };
	glNamedBufferSubData(m_counters, 0, static_cast<GLsizeiptr>(sizeof(zeros)), zeros.data());
}

struct FrameParamsStd140 {
	float dt;
	float _pad0[3];
	float gravity[4];    // vec3 + pad
	unsigned int maxParticles;
	int _pad1[3];
};

void ParticleSystemManager::Tick(float dt) {
	PROFILE_ZONE;

	// Update emitters and collect per-emitter spawn requests
	std::vector<std::pair<ParticleEmitter*, uint32_t>> spawnList;
	uint32_t totalSpawns = 0;
	for (auto* e : m_emitters) {
		if (!e) {
			continue;
		}
		e->Update(dt);
		uint32_t cnt = e->ConsumeSpawnRequests();
		if (cnt > 0) {
			spawnList.emplace_back(e, cnt);
			totalSpawns += cnt;
		}
	}

	// Clamp total spawns to available capacity
	uint32_t available = m_maxParticles > m_inCount ? (m_maxParticles - m_inCount) : 0;
	uint32_t spawnWriteTotal = std::min(totalSpawns, available);

	// If there are spawn particles, write per-emitter entries into m_ssboOut starting at offset 0
	if (spawnWriteTotal > 0) {
		GLsizeiptr writeSize = static_cast<GLsizeiptr>(spawnWriteTotal) * PARTICLE_SIZE;
		void* ptr = glMapNamedBufferRange(m_ssboOut, 0, writeSize, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT);
		if (ptr) {
			char* base = static_cast<char*>(ptr);
			uint32_t written = 0;
			for (auto& kv : spawnList) {
				ParticleEmitter* e = kv.first;
				uint32_t cnt = kv.second;
				uint32_t toWrite = std::min(cnt, spawnWriteTotal - written);
				for (uint32_t i = 0; i < toWrite; ++i) {
					float* f = reinterpret_cast<float*>(base + (written + i) * PARTICLE_SIZE);
					// layout: f[0-3]=pos.xyz + startSize in .w
					f[0] = e->position.x;
					f[1] = e->position.y;
					f[2] = e->position.z;
					f[3] = e->startSize;
					// vel (f[4-7])
					f[4] = e->startVelocity.x;
					f[5] = e->startVelocity.y;
					f[6] = e->startVelocity.z;
					f[7] = 0.0f;
					// startColor (f[8-11])
					f[8] = e->startColor.r;
					f[9] = e->startColor.g;
					f[10] = e->startColor.b;
					f[11] = e->startColor.a;
					// endColor + endSize packed in .w (f[12-15])
					f[12] = e->endColor.r;
					f[13] = e->endColor.g;
					f[14] = e->endColor.b;
					f[15] = e->endColor.a;
					// curColor (f[16-19]) - start as startColor
					f[16] = e->startColor.r;
					f[17] = e->startColor.g;
					f[18] = e->startColor.b;
					f[19] = e->startColor.a;
					// lifeRemaining, lifeMax, seed, endSize (f[20-23])
					f[20] = e->startLife;
					f[21] = e->startLife;
					f[22] = static_cast<float>(e->GetId());
					f[23] = e->endSize;
					written++;
				}
				if (written >= spawnWriteTotal) {
					break;
				}
			}
			glUnmapNamedBuffer(m_ssboOut);

			// Set counters: inCount (existing), outCount = spawnWriteTotal (initial), spawnCount = spawnWriteTotal
			std::array<uint32_t, 4> countersVals = { m_inCount, spawnWriteTotal, spawnWriteTotal, 0 };
			glNamedBufferSubData(m_counters, 0, static_cast<GLsizeiptr>(sizeof(countersVals)), countersVals.data());
		}
	} else {
		// ensure counters.inCount has current inCount
		std::array<uint32_t, 4> countersVals = { m_inCount, 0, 0, 0 };
		glNamedBufferSubData(m_counters, 0, static_cast<GLsizeiptr>(sizeof(countersVals)), countersVals.data());
	}

	// Bind buffers to the layout expected by the compute shader
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_ssboIn);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_ssboOut);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_emitRequests);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_counters);

	// Upload FrameParams UBO
	FrameParamsStd140 fp {};
	fp.dt = dt;
	fp.gravity[0] = m_gravity.x;
	fp.gravity[1] = m_gravity.y;
	fp.gravity[2] = m_gravity.z;
	fp.gravity[3] = 0.0f;
	fp.maxParticles = m_maxParticles;
	glNamedBufferSubData(m_uboFrameParams, 0, static_cast<GLsizeiptr>(sizeof(fp)), &fp);

	// dispatch groups based on current inCount, if inCount == 0, still dispatch a small group to handle spawns
	uint32_t groups = (std::max<uint32_t>(1, (m_inCount + 255) / 256));
	glDispatchCompute(groups, 1, 1);

	// Ensure writes are visible to vertex stage
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

	// After compute, swap buffers so next frame reads from updated buffer
	SwapBuffers();

	// Read back counters.outCount to update m_inCount for next frame every 5 frames
	m_readbackCounter = (m_readbackCounter + 1) % std::max(1, m_readbackInterval);
	if (m_readbackCounter == 0) {
		std::array<uint32_t, 4> countersRead = { 0u, 0u, 0u, 0u };
		glGetNamedBufferSubData(m_counters, 0, static_cast<GLsizeiptr>(sizeof(countersRead)), countersRead.data());
		m_inCount = countersRead[1];
	}
	// else keep m_inCount unchanged
}

void ParticleSystemManager::Render(const glm::mat4& viewProj) {
	PROFILE_ZONE;
	if (!m_renderShader) {
		return;
	}
	
	glm::mat4 inv = glm::inverse(viewProj);
	glm::vec3 camRight = glm::normalize(glm::vec3(inv[0][0], inv[1][0], inv[2][0]));
	glm::vec3 camUp = glm::normalize(glm::vec3(inv[0][1], inv[1][1], inv[2][1]));

	
	DefaultTexture->Bind(1);    // TODO fix hardcoded texture test
	m_renderShader->Use();
	m_renderShader->Set("u_ViewProj", viewProj);
	m_renderShader->Set("u_CamRight", camRight);
	m_renderShader->Set("u_CamUp", camUp);

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_ssboIn);
	m_renderShader->SetSampler("u_Tex", 1);

	if (m_quadMesh && m_inCount > 0) {
		m_quadMesh->bind();
		glDepthMask(GL_FALSE);
		glDrawArraysInstanced(GL_TRIANGLES, 0, static_cast<GLsizei>(m_quadMesh->GetVertexCount()), static_cast<GLsizei>(m_inCount));
		glDepthMask(GL_TRUE);
		m_quadMesh->unbind();
	}
}

ParticleEmitter* ParticleSystemManager::CreateEmitter(uint32_t maxParticles) {
	auto* e = new ParticleEmitter(maxParticles);
	m_emitters.push_back(e);
	return e;
}

void ParticleSystemManager::DestroyEmitter(ParticleEmitter* emitter) {
	auto it = std::find(m_emitters.begin(), m_emitters.end(), emitter);
	if (it != m_emitters.end()) {
		m_emitters.erase(it);
	}
	delete emitter;
}

}    // namespace renderer
