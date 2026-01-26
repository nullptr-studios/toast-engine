/// @file ParticleSystem.cpp
/// @date 01/20/2026
/// @brief GPU-based Particle System implementation with multi-emitter and Lua serialization

#include "Toast/Objects/ParticleSystem.hpp"

#include "Toast/Log.hpp"
#include "Toast/Profiler.hpp"
#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Renderer/OclussionVolume.hpp"
#include "Toast/Resources/ResourceManager.hpp"
#include "Toast/Time.hpp"

#include <glad/gl.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <fstream>

#include <random>

#ifdef TOAST_EDITOR
#include "imgui.h"
#include "imgui_stdlib.h"
#endif

namespace toast {


static glm::vec3 ParseVec3FromLua(const sol::object& obj, const glm::vec3& defaultVal = glm::vec3(0.0f)) {
	if (!obj.valid() || !obj.is<sol::table>()) {
		return defaultVal;
	}
	sol::table t = obj.as<sol::table>();
	return glm::vec3(
		t[1].get_or<float>(defaultVal.x),
		t[2].get_or<float>(defaultVal.y),
		t[3].get_or<float>(defaultVal.z)
	);
}

static glm::vec4 ParseVec4FromLua(const sol::object& obj, const glm::vec4& defaultVal = glm::vec4(1.0f)) {
	if (!obj.valid() || !obj.is<sol::table>()) {
		return defaultVal;
	}
	sol::table t = obj.as<sol::table>();
	return glm::vec4(
		t[1].get_or<float>(defaultVal.x),
		t[2].get_or<float>(defaultVal.y),
		t[3].get_or<float>(defaultVal.z),
		t[4].get_or<float>(defaultVal.w)
	);
}

static EmissionMode ParseEmissionMode(const std::string& mode) {
	if (mode == "burst" || mode == "Burst") return EmissionMode::Burst;
	return EmissionMode::Continuous;
}

static EmitterShape ParseEmitterShape(const std::string& shape) {
	if (shape == "sphere" || shape == "Sphere") return EmitterShape::Sphere;
	if (shape == "box" || shape == "Box") return EmitterShape::Box;
	if (shape == "cone" || shape == "Cone") return EmitterShape::Cone;
	return EmitterShape::Point;
}


void ParticleBurst::LoadFromLua(const sol::table& table) {
	sol::optional<float> timeOpt = table["time"];
	time = timeOpt.value_or(0.0f);
	sol::optional<uint32_t> countOpt = table["count"];
	count = countOpt.value_or(10);
	sol::optional<float> cycleOpt = table["cycleInterval"];
	cycleInterval = cycleOpt.value_or(0.0f);
	triggered = false;
}


void ParticleEmitterConfig::LoadFromLua(const sol::table& table) {
	// Identification
	name = table["name"].get_or<std::string>("Emitter");
	sol::optional<bool> enabledOpt = table["enabled"];
	enabled = enabledOpt.value_or(true);
	
	// Emission
	sol::optional<std::string> modeStr = table["emissionMode"];
	if (modeStr) {
		emissionMode = ParseEmissionMode(*modeStr);
	}
	sol::optional<float> emissionRateOpt = table["emissionRate"];
	emissionRate = emissionRateOpt.value_or(10.0f);
	
	// Bursts
	sol::optional<sol::table> burstsTable = table["bursts"];
	if (burstsTable) {
		bursts.clear();
		for (auto& [_, burstObj] : *burstsTable) {
			if (burstObj.is<sol::table>()) {
				ParticleBurst burst;
				burst.LoadFromLua(burstObj.as<sol::table>());
				bursts.push_back(burst);
			}
		}
	}
	
	// Shape
	sol::optional<std::string> shapeStr = table["shape"];
	if (shapeStr) {
		shape = ParseEmitterShape(*shapeStr);
	}
	shapeSize = ParseVec3FromLua(table["shapeSize"], glm::vec3(1.0f));
	sol::optional<float> coneAngleOpt = table["coneAngle"];
	coneAngle = coneAngleOpt.value_or(45.0f);
	
	// Offset
	localOffset = ParseVec3FromLua(table["localOffset"], glm::vec3(0.0f));
	
	// Lifetime
	sol::optional<sol::object> lifetimeObj = table["lifetime"];
	if (lifetimeObj) {
		lifetime.LoadFromLua(*lifetimeObj);
	}
	
	// Velocity
	sol::optional<sol::object> speedObj = table["speed"];
	if (speedObj) {
		speed.LoadFromLua(*speedObj);
	}
	direction = ParseVec3FromLua(table["direction"], glm::vec3(0.0f, 1.0f, 0.0f));
	sol::optional<float> dirRandomOpt = table["directionRandomness"];
	directionRandomness = dirRandomOpt.value_or(0.0f);
	
	// Size
	sol::optional<sol::object> startSizeObj = table["startSize"];
	if (startSizeObj) {
		startSize.LoadFromLua(*startSizeObj);
	}
	sol::optional<sol::object> endSizeObj = table["endSize"];
	if (endSizeObj) {
		endSize.LoadFromLua(*endSizeObj);
	}
	
	// Rotation
	sol::optional<sol::object> startRotObj = table["startRotation"];
	if (startRotObj) {
		startRotation.LoadFromLua(*startRotObj);
	}
	sol::optional<sol::object> rotSpeedObj = table["rotationSpeed"];
	if (rotSpeedObj) {
		rotationSpeed.LoadFromLua(*rotSpeedObj);
	}
	
	// Color
	startColor = ParseVec4FromLua(table["startColor"], glm::vec4(1.0f));
	endColor = ParseVec4FromLua(table["endColor"], glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
	sol::optional<bool> randColorOpt = table["randomizeStartColor"];
	randomizeStartColor = randColorOpt.value_or(false);
	startColorRangeMin = ParseVec4FromLua(table["startColorRangeMin"], glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
	startColorRangeMax = ParseVec4FromLua(table["startColorRangeMax"], glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
	
	// Physics
	gravity = ParseVec3FromLua(table["gravity"], glm::vec3(0.0f, -9.81f, 0.0f));
	sol::optional<float> dragOpt = table["drag"];
	drag = dragOpt.value_or(0.0f);
	
	// Texture
	texturePath = table["texturePath"].get_or<std::string>("");
	sol::optional<bool> useTexOpt = table["useTexture"];
	useTexture = useTexOpt.value_or(!texturePath.empty());
	sol::optional<bool> additiveOpt = table["additiveBlending"];
	additiveBlending = additiveOpt.value_or(false);
	
	// Max particles
	sol::optional<uint32_t> maxParticlesOpt = table["maxParticles"];
	maxParticles = maxParticlesOpt.value_or(10000);
}

void ParticleEmitterConfig::ApplyPreset(const std::string& presetName) {
	if (presetName == "Smoke") {
		emissionMode = EmissionMode::Continuous;
		emissionRate = 20.0f;
		shape = EmitterShape::Sphere;
		shapeSize = glm::vec3(0.5f);
		lifetime = { 2.0f, 4.0f };
		speed = { 0.5f, 1.5f };
		direction = glm::vec3(0.0f, 1.0f, 0.0f);
		directionRandomness = 0.3f;
		startSize = { 0.3f, 0.5f };
		endSize = { 1.0f, 2.0f };
		startColor = glm::vec4(0.5f, 0.5f, 0.5f, 0.6f);
		endColor = glm::vec4(0.3f, 0.3f, 0.3f, 0.0f);
		gravity = glm::vec3(0.0f, 0.2f, 0.0f);
		drag = 0.5f;
		additiveBlending = false;
	} else if (presetName == "Fire") {
		emissionMode = EmissionMode::Continuous;
		emissionRate = 50.0f;
		shape = EmitterShape::Cone;
		coneAngle = 15.0f;
		lifetime = { 0.5f, 1.5f };
		speed = { 2.0f, 4.0f };
		direction = glm::vec3(0.0f, 1.0f, 0.0f);
		startSize = { 0.2f, 0.4f };
		endSize = { 0.05f, 0.1f };
		startColor = glm::vec4(1.0f, 0.8f, 0.2f, 1.0f);
		endColor = glm::vec4(1.0f, 0.2f, 0.0f, 0.0f);
		gravity = glm::vec3(0.0f, 1.0f, 0.0f);
		drag = 0.2f;
		additiveBlending = true;
	} else if (presetName == "Sparks") {
		emissionMode = EmissionMode::Burst;
		bursts = { { 0.0f, 50, 0.5f, false } };
		shape = EmitterShape::Point;
		lifetime = { 0.3f, 0.8f };
		speed = { 5.0f, 10.0f };
		direction = glm::vec3(0.0f, 1.0f, 0.0f);
		directionRandomness = 1.0f;
		startSize = { 0.05f, 0.1f };
		endSize = { 0.01f, 0.02f };
		startColor = glm::vec4(1.0f, 0.9f, 0.5f, 1.0f);
		endColor = glm::vec4(1.0f, 0.5f, 0.0f, 0.0f);
		gravity = glm::vec3(0.0f, -15.0f, 0.0f);
		drag = 0.0f;
		additiveBlending = true;
	} else if (presetName == "Snow") {
		emissionMode = EmissionMode::Continuous;
		emissionRate = 30.0f;
		shape = EmitterShape::Box;
		shapeSize = glm::vec3(10.0f, 0.1f, 10.0f);
		lifetime = { 3.0f, 5.0f };
		speed = { 0.2f, 0.5f };
		direction = glm::vec3(0.0f, -1.0f, 0.0f);
		directionRandomness = 0.1f;
		startSize = { 0.05f, 0.15f };
		endSize = { 0.05f, 0.15f };
		startColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.8f);
		endColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
		gravity = glm::vec3(0.0f, -0.5f, 0.0f);
		drag = 0.3f;
		additiveBlending = false;
	} else if (presetName == "Explosion") {
		emissionMode = EmissionMode::Burst;
		bursts = { { 0.0f, 100, 0.0f, false } };
		shape = EmitterShape::Point;
		lifetime = { 0.5f, 1.5f };
		speed = { 3.0f, 8.0f };
		direction = glm::vec3(0.0f, 1.0f, 0.0f);
		directionRandomness = 1.0f;
		startSize = { 0.3f, 0.6f };
		endSize = { 0.1f, 0.2f };
		startColor = glm::vec4(1.0f, 0.6f, 0.1f, 1.0f);
		endColor = glm::vec4(0.3f, 0.1f, 0.0f, 0.0f);
		gravity = glm::vec3(0.0f, -5.0f, 0.0f);
		drag = 1.0f;
		additiveBlending = true;
	}
}


ParticleEmitter::ParticleEmitter()
	: m_rng(std::random_device{}()) {
}

ParticleEmitter::~ParticleEmitter() {
	CleanupGPUResources();
}

ParticleEmitter::ParticleEmitter(ParticleEmitter&& other) noexcept
	: m_config(std::move(other.m_config)),
	  m_isPlaying(other.m_isPlaying),
	  m_gpuInitialized(other.m_gpuInitialized),
	  m_systemTime(other.m_systemTime),
	  m_emissionAccumulator(other.m_emissionAccumulator),
	  m_aliveCount(other.m_aliveCount),
	  m_counterBuffer(other.m_counterBuffer),
	  m_counterBufferPtr(other.m_counterBufferPtr),
	  m_frameParamsUBO(other.m_frameParamsUBO),
	  m_currentBuffer(other.m_currentBuffer),
	  m_computeShader(std::move(other.m_computeShader)),
	  m_renderShader(std::move(other.m_renderShader)),
	  m_texture(std::move(other.m_texture)),
	  m_rng(std::move(other.m_rng)),
	  m_dist(other.m_dist) {
	m_particleBuffers[0] = other.m_particleBuffers[0];
	m_particleBuffers[1] = other.m_particleBuffers[1];
	
	// Clear other's resources to prevent double cleanup
	other.m_particleBuffers[0] = 0;
	other.m_particleBuffers[1] = 0;
	other.m_counterBuffer = 0;
	other.m_counterBufferPtr = nullptr;
	other.m_frameParamsUBO = 0;
	other.m_gpuInitialized = false;
}

ParticleEmitter& ParticleEmitter::operator=(ParticleEmitter&& other) noexcept {
	if (this != &other) {
		CleanupGPUResources();
		
		m_config = std::move(other.m_config);
		m_isPlaying = other.m_isPlaying;
		m_gpuInitialized = other.m_gpuInitialized;
		m_systemTime = other.m_systemTime;
		m_emissionAccumulator = other.m_emissionAccumulator;
		m_aliveCount = other.m_aliveCount;
		m_particleBuffers[0] = other.m_particleBuffers[0];
		m_particleBuffers[1] = other.m_particleBuffers[1];
		m_counterBuffer = other.m_counterBuffer;
		m_counterBufferPtr = other.m_counterBufferPtr;
		m_frameParamsUBO = other.m_frameParamsUBO;
		m_currentBuffer = other.m_currentBuffer;
		m_computeShader = std::move(other.m_computeShader);
		m_renderShader = std::move(other.m_renderShader);
		m_texture = std::move(other.m_texture);
		m_rng = std::move(other.m_rng);
		m_dist = other.m_dist;
		
		other.m_particleBuffers[0] = 0;
		other.m_particleBuffers[1] = 0;
		other.m_counterBuffer = 0;
		other.m_counterBufferPtr = nullptr;
		other.m_frameParamsUBO = 0;
		other.m_gpuInitialized = false;
	}
	return *this;
}

void ParticleEmitter::InitGPUResources(const std::shared_ptr<renderer::Shader>& computeShader,
                                       const std::shared_ptr<renderer::Shader>& renderShader) {
	if (m_gpuInitialized) return;
	
	m_computeShader = computeShader;
	m_renderShader = renderShader;
	
	// Load texture
	LoadTexture();
	
	// Create double-buffered particle SSBOs
	const size_t bufferSize = sizeof(GPUParticle) * m_config.maxParticles;
	
	glGenBuffers(2, m_particleBuffers);
	for (int i = 0; i < 2; ++i) {
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_particleBuffers[i]);
		glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, nullptr, GL_DYNAMIC_DRAW);
	}
	
	// Create counter buffer with persistent mapping
	glGenBuffers(1, &m_counterBuffer);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_counterBuffer);
	
	GLbitfield storageFlags = GL_MAP_READ_BIT | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
	glBufferStorage(GL_SHADER_STORAGE_BUFFER, sizeof(uint32_t) * 4, nullptr, storageFlags);
	m_counterBufferPtr = static_cast<uint32_t*>(glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t) * 4, storageFlags));
	
	if (m_counterBufferPtr) {
		m_counterBufferPtr[0] = 0;
		m_counterBufferPtr[1] = 0;
		m_counterBufferPtr[2] = 0;
		m_counterBufferPtr[3] = 0;
	}
	
	// Create frame parameters UBO
	glGenBuffers(1, &m_frameParamsUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, m_frameParamsUBO);
	float frameParams[8] = { 0 };
	glBufferData(GL_UNIFORM_BUFFER, sizeof(frameParams), frameParams, GL_DYNAMIC_DRAW);
	
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	
	m_gpuInitialized = true;
	m_currentBuffer = 0;
}

void ParticleEmitter::CleanupGPUResources() {
	if (!m_gpuInitialized) return;
	
	if (m_counterBuffer && m_counterBufferPtr) {
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_counterBuffer);
		glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
		m_counterBufferPtr = nullptr;
	}
	
	if (m_particleBuffers[0]) {
		glDeleteBuffers(2, m_particleBuffers);
		m_particleBuffers[0] = m_particleBuffers[1] = 0;
	}
	if (m_counterBuffer) {
		glDeleteBuffers(1, &m_counterBuffer);
		m_counterBuffer = 0;
	}
	if (m_frameParamsUBO) {
		glDeleteBuffers(1, &m_frameParamsUBO);
		m_frameParamsUBO = 0;
	}
	
	m_texture.reset();
	m_gpuInitialized = false;
}

void ParticleEmitter::LoadTexture() {
	if (m_config.useTexture && !m_config.texturePath.empty()) {
		m_texture = resource::ResourceManager::GetInstance()->LoadResource<Texture>(m_config.texturePath);
	} else {
		m_texture.reset();
	}
}

void ParticleEmitter::Stop() {
	m_isPlaying = false;
	m_aliveCount = 0;
	m_systemTime = 0.0f;
	m_emissionAccumulator = 0.0f;
	
	for (auto& burst : m_config.bursts) {
		burst.triggered = false;
	}
	
	if (m_counterBufferPtr) {
		m_counterBufferPtr[0] = 0;
		m_counterBufferPtr[1] = 0;
		m_counterBufferPtr[2] = 0;
		m_counterBufferPtr[3] = 0;
	}
}

void ParticleEmitter::UpdateAndRender(const glm::mat4& viewProjection,
                                      const glm::vec3& worldPos,
                                      const glm::vec3& camRight,
                                      const glm::vec3& camUp,
                                      float deltaTime) {
	if (!m_gpuInitialized || !m_computeShader || !m_renderShader || !m_config.enabled) {
		return;
	}
	
	// Update system time and handle emission
	if (m_isPlaying) {
		m_systemTime += deltaTime;
		
		if (m_config.emissionMode == EmissionMode::Continuous) {
			m_emissionAccumulator += m_config.emissionRate * deltaTime;
			const uint32_t toSpawn = static_cast<uint32_t>(m_emissionAccumulator);
			if (toSpawn > 0) {
				SpawnParticles(toSpawn, worldPos + m_config.localOffset);
				m_emissionAccumulator -= static_cast<float>(toSpawn);
			}
		}
		
		for (auto& burst : m_config.bursts) {
			if (!burst.triggered && m_systemTime >= burst.time) {
				SpawnParticles(burst.count, worldPos + m_config.localOffset);
				burst.triggered = true;
			}
			if (burst.cycleInterval > 0.0f && burst.triggered) {
				const float cycleTime = fmod(m_systemTime - burst.time, burst.cycleInterval);
				if (cycleTime < deltaTime) {
					SpawnParticles(burst.count, worldPos + m_config.localOffset);
				}
			}
		}
	}
	
	if (m_aliveCount == 0) return;
	
	// COMPUTE PASS
	struct FrameParams {
		float dt;
		float gravityX, gravityY, gravityZ;
		uint32_t maxParticles;
		float drag;
		float pad1, pad2;
	} params;
	
	params.dt = deltaTime;
	params.gravityX = m_config.gravity.x;
	params.gravityY = m_config.gravity.y;
	params.gravityZ = m_config.gravity.z;
	params.maxParticles = m_config.maxParticles;
	params.drag = m_config.drag;
	params.pad1 = params.pad2 = 0.0f;
	
	glBindBuffer(GL_UNIFORM_BUFFER, m_frameParamsUBO);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(params), &params);
	
	if (m_counterBufferPtr) {
		m_counterBufferPtr[1] = 0;
		m_counterBufferPtr[0] = m_aliveCount;
	}
	
	glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
	
	const int readBuffer = m_currentBuffer;
	const int writeBuffer = 1 - m_currentBuffer;
	
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_particleBuffers[readBuffer]);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_particleBuffers[writeBuffer]);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_counterBuffer);
	glBindBufferBase(GL_UNIFORM_BUFFER, 4, m_frameParamsUBO);
	
	m_computeShader->Use();
	
	const uint32_t workGroups = (m_aliveCount + 255) / 256;
	glDispatchCompute(workGroups, 1, 1);
	
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
	
	GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	glClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT, GL_TIMEOUT_IGNORED);
	glDeleteSync(sync);
	
	if (m_counterBufferPtr) {
		m_aliveCount = m_counterBufferPtr[1];
	}
	
	m_currentBuffer = writeBuffer;
	
	if (m_aliveCount == 0) return;
	
	// RENDER PASS
	glEnable(GL_BLEND);
	if (m_config.additiveBlending) {
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	} else {
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	
	glDepthMask(GL_FALSE);
	
	m_renderShader->Use();
	m_renderShader->Set("u_ViewProj", viewProjection);
	m_renderShader->Set("u_CamRight", camRight);
	m_renderShader->Set("u_CamUp", camUp);
	
	if (m_texture && m_config.useTexture) {
		m_texture->Bind(1);
		m_renderShader->SetSampler("u_Tex", 1);
	}
	
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_particleBuffers[m_currentBuffer]);
}

void ParticleEmitter::SpawnParticles(uint32_t count, const glm::vec3& worldPos) {
	if (!m_gpuInitialized || count == 0) return;
	
	const uint32_t available = m_config.maxParticles - m_aliveCount;
	count = std::min(count, available);
	
	if (count == 0) return;
	
	std::vector<GPUParticle> newParticles(count);
	
	for (uint32_t i = 0; i < count; ++i) {
		GPUParticle& p = newParticles[i];
		
		glm::vec3 spawnOffset = GenerateSpawnPosition();
		glm::vec3 pos = worldPos + spawnOffset;
		
		float startSize = RandomFloat(m_config.startSize.min, m_config.startSize.max);
		float endSize = RandomFloat(m_config.endSize.min, m_config.endSize.max);
		
		p.pos = glm::vec4(pos, startSize);
		
		glm::vec3 vel = GenerateSpawnVelocity();
		float rotation = glm::radians(RandomFloat(m_config.startRotation.min, m_config.startRotation.max));
		p.vel = glm::vec4(vel, rotation);
		
		glm::vec4 startCol = m_config.startColor;
		if (m_config.randomizeStartColor) {
			startCol = glm::vec4(
				RandomFloat(m_config.startColorRangeMin.r, m_config.startColorRangeMax.r),
				RandomFloat(m_config.startColorRangeMin.g, m_config.startColorRangeMax.g),
				RandomFloat(m_config.startColorRangeMin.b, m_config.startColorRangeMax.b),
				RandomFloat(m_config.startColorRangeMin.a, m_config.startColorRangeMax.a)
			);
		}
		p.color = startCol;
		p.end = m_config.endColor;
		
		float lifetime = RandomFloat(m_config.lifetime.min, m_config.lifetime.max);
		float seed = m_dist(m_rng);
		p.misc = glm::vec4(lifetime, lifetime, seed, endSize);
		
		float rotSpeed = glm::radians(RandomFloat(m_config.rotationSpeed.min, m_config.rotationSpeed.max));
		p.extra = glm::vec4(startSize, rotSpeed, m_config.drag, 0.0f);
	}
	
	const int writeBuffer = m_currentBuffer;
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_particleBuffers[writeBuffer]);
	
	const size_t offset = sizeof(GPUParticle) * m_aliveCount;
	const size_t size = sizeof(GPUParticle) * count;
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), newParticles.data());
	
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	
	m_aliveCount += count;
}

glm::vec3 ParticleEmitter::GenerateSpawnPosition() {
	switch (m_config.shape) {
		case EmitterShape::Sphere: {
			glm::vec3 p;
			do {
				p = glm::vec3(
					RandomFloat(-1.0f, 1.0f),
					RandomFloat(-1.0f, 1.0f),
					RandomFloat(-1.0f, 1.0f)
				);
			} while (glm::dot(p, p) > 1.0f);
			return p * m_config.shapeSize.x;
		}
		
		case EmitterShape::Box:
			return glm::vec3(
				RandomFloat(-m_config.shapeSize.x, m_config.shapeSize.x),
				RandomFloat(-m_config.shapeSize.y, m_config.shapeSize.y),
				RandomFloat(-m_config.shapeSize.z, m_config.shapeSize.z)
			) * 0.5f;
		
		case EmitterShape::Point:
		case EmitterShape::Cone:
		default:
			return glm::vec3(0.0f);
	}
}

glm::vec3 ParticleEmitter::GenerateSpawnVelocity() {
	const float speed = RandomFloat(m_config.speed.min, m_config.speed.max);
	
	glm::vec3 dir = glm::normalize(m_config.direction);
	
	if (m_config.shape == EmitterShape::Cone) {
		const float halfAngle = glm::radians(m_config.coneAngle);
		const float cosAngle = std::cos(halfAngle);
		
		const float z = RandomFloat(cosAngle, 1.0f);
		const float phi = RandomFloat(0.0f, 2.0f * glm::pi<float>());
		const float sinTheta = std::sqrt(1.0f - z * z);
		
		glm::vec3 localDir(sinTheta * std::cos(phi), sinTheta * std::sin(phi), z);
		
		glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f);
		if (std::abs(glm::dot(dir, up)) > 0.999f) {
			up = glm::vec3(1.0f, 0.0f, 0.0f);
		}
		glm::vec3 right = glm::normalize(glm::cross(up, dir));
		up = glm::cross(dir, right);
		
		dir = localDir.x * right + localDir.y * up + localDir.z * dir;
	} else if (m_config.directionRandomness > 0.0f) {
		glm::vec3 randomDir = RandomDirection();
		dir = glm::normalize(glm::mix(dir, randomDir, m_config.directionRandomness));
	}
	
	return dir * speed;
}

float ParticleEmitter::RandomFloat(float min, float max) {
	return min + m_dist(m_rng) * (max - min);
}

glm::vec3 ParticleEmitter::RandomDirection() {
	const float theta = RandomFloat(0.0f, 2.0f * glm::pi<float>());
	const float phi = std::acos(RandomFloat(-1.0f, 1.0f));
	
	return glm::vec3(
		std::sin(phi) * std::cos(theta),
		std::sin(phi) * std::sin(theta),
		std::cos(phi)
	);
}


void ParticleSystem::OnRender(const glm::mat4& viewProjection) noexcept {
	if (!m_sharedResourcesInitialized) return;
	
	if (!OclussionVolume::isSphereOnPlanes(renderer::IRendererBase::GetInstance()->GetFrustumPlanes(), worldPosition(), m_cullingRadius)) {
		return;
	}
	
	PROFILE_ZONE;
	
	const float dt = static_cast<float>(Time::delta());
	const glm::vec3 worldPos = worldPosition();
	
	glm::mat4 viewMatrix = renderer::IRendererBase::GetInstance()->GetViewMatrix();
	glm::vec3 camRight = glm::vec3(viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0]);
	glm::vec3 camUp = glm::vec3(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);
	
	// Bind shared quad VAO once
	glBindVertexArray(m_quadVAO);
	
	for (auto& emitter : m_emitters) {
		if (!emitter.IsGPUInitialized()) continue;
		
		emitter.UpdateAndRender(viewProjection, worldPos, camRight, camUp, dt);
		
		// Draw this emitter's particles
		uint32_t count = emitter.GetParticleCount();
		if (count > 0) {
			glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(count));
		}
	}
	
	glBindVertexArray(0);
	
	// Restore state
	glDepthMask(GL_TRUE);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

ParticleSystem::ParticleSystem() = default;

ParticleSystem::~ParticleSystem() {
	CleanupSharedResources();
}

void ParticleSystem::Init() {
	TransformComponent::Init();
	
	InitSharedResources();
	
	// If no emitters exist, add a default one
	if (m_emitters.empty()) {
		AddEmitterWithPreset("Smoke");
	}
	
	renderer::IRendererBase::GetInstance()->AddRenderable(this);
	
	TOAST_INFO("ParticleSystem initialized with {} emitter(s)", m_emitters.size());
}

void ParticleSystem::Destroy() {
	renderer::IRendererBase::GetInstance()->RemoveRenderable(this);
	
	for (auto& emitter : m_emitters) {
		emitter.CleanupGPUResources();
	}
	m_emitters.clear();
	
	CleanupSharedResources();
	TransformComponent::Destroy();
}

void ParticleSystem::Tick() {
	TransformComponent::Tick();
}

void ParticleSystem::Load(json_t j, bool force_create) {
	TransformComponent::Load(j, force_create);
	
	// Load Lua config path
	if (j.contains("luaConfigPath")) {
		m_luaConfigPath = j["luaConfigPath"].get<std::string>();
		if (!m_luaConfigPath.empty()) {
			LoadFromLua(m_luaConfigPath);
		}
	}
	
	// Load culling radius
	if (j.contains("cullingRadius")) {
		m_cullingRadius = j["cullingRadius"].get<int>();
	}
}

json_t ParticleSystem::Save() const {
	json_t j = TransformComponent::Save();
	
	// Just save the Lua config path
	j["luaConfigPath"] = m_luaConfigPath;
	j["cullingRadius"] = m_cullingRadius;
	
	return j;
}

bool ParticleSystem::LoadFromLua(const std::string& luaPath) {
	sol::state lua;
	
	try {
		lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::table);
		
		std::string current_path = lua["package"]["path"];
		std::string custom_path = ";./assets/?.lua;./assets/particles/?.lua";
		lua["package"]["path"] = current_path + custom_path;
		
		auto file = resource::Open(luaPath);
		if (!file.has_value()) {
			TOAST_ERROR("Particle system config file couldn't be opened: {}", luaPath);
			return false;
		}
		
		sol::optional<sol::table> result = lua.script(*file);
		if (!result.has_value()) {
			TOAST_ERROR("Particle system config file didn't return anything: {}", luaPath);
			return false;
		}
		
		sol::table config = *result;
		
		// Verify format
		sol::optional<std::string> format = config["format"];
		if (!format.has_value() || *format != "particle_system") {
			TOAST_ERROR("Particle system config has incorrect format, expected 'particle_system'");
			return false;
		}
		
		// Store path for reloading
		m_luaConfigPath = luaPath;
		
		// Clear existing emitters
		for (auto& emitter : m_emitters) {
			emitter.CleanupGPUResources();
		}
		m_emitters.clear();
		
		// Load emitters
		sol::optional<sol::table> emittersTable = config["emitters"];
		if (emittersTable) {
			for (auto& [_, emitterObj] : *emittersTable) {
				if (emitterObj.is<sol::table>()) {
					ParticleEmitter& emitter = AddEmitter();
					emitter.GetConfig().LoadFromLua(emitterObj.as<sol::table>());
					
					// Reinitialize with new config if GPU resources already initialized
					if (m_sharedResourcesInitialized) {
						emitter.CleanupGPUResources();
						emitter.InitGPUResources(m_computeShader, m_renderShader);
					}
				}
			}
		}
		
		TOAST_INFO("Loaded particle system config from Lua: {} ({} emitters)", luaPath, m_emitters.size());
		return true;
		
	} catch (const sol::error& e) {
		TOAST_ERROR("Failed to parse particle system Lua config: {}", e.what());
		return false;
	}
}

bool ParticleSystem::SaveToLua(const std::string& luaPath) const {
	// Generate Lua file content
	std::ostringstream lua;
	lua << "-- Particle System Configuration\n";
	lua << "-- Auto-generated\n\n";
	lua << "return {\n";
	lua << "    format = \"particle_system\",\n";
	lua << "    emitters = {\n";
	
	for (size_t i = 0; i < m_emitters.size(); ++i) {
		const auto& config = m_emitters[i].GetConfig();
		lua << "        {\n";
		lua << "            name = \"" << config.name << "\",\n";
		lua << "            enabled = " << (config.enabled ? "true" : "false") << ",\n";
		lua << "            emissionMode = \"" << (config.emissionMode == EmissionMode::Burst ? "burst" : "continuous") << "\",\n";
		lua << "            emissionRate = " << config.emissionRate << ",\n";
		
		// Shape
		std::string shapeStr = "point";
		if (config.shape == EmitterShape::Sphere) shapeStr = "sphere";
		else if (config.shape == EmitterShape::Box) shapeStr = "box";
		else if (config.shape == EmitterShape::Cone) shapeStr = "cone";
		lua << "            shape = \"" << shapeStr << "\",\n";
		lua << "            shapeSize = {" << config.shapeSize.x << ", " << config.shapeSize.y << ", " << config.shapeSize.z << "},\n";
		lua << "            coneAngle = " << config.coneAngle << ",\n";
		lua << "            localOffset = {" << config.localOffset.x << ", " << config.localOffset.y << ", " << config.localOffset.z << "},\n";
		
		// Lifetime/Speed
		lua << "            lifetime = {" << config.lifetime.min << ", " << config.lifetime.max << "},\n";
		lua << "            speed = {" << config.speed.min << ", " << config.speed.max << "},\n";
		lua << "            direction = {" << config.direction.x << ", " << config.direction.y << ", " << config.direction.z << "},\n";
		lua << "            directionRandomness = " << config.directionRandomness << ",\n";
		
		// Size
		lua << "            startSize = {" << config.startSize.min << ", " << config.startSize.max << "},\n";
		lua << "            endSize = {" << config.endSize.min << ", " << config.endSize.max << "},\n";
		
		// Rotation
		lua << "            startRotation = {" << config.startRotation.min << ", " << config.startRotation.max << "},\n";
		lua << "            rotationSpeed = {" << config.rotationSpeed.min << ", " << config.rotationSpeed.max << "},\n";
		
		// Color
		lua << "            startColor = {" << config.startColor.r << ", " << config.startColor.g << ", " << config.startColor.b << ", " << config.startColor.a << "},\n";
		lua << "            endColor = {" << config.endColor.r << ", " << config.endColor.g << ", " << config.endColor.b << ", " << config.endColor.a << "},\n";
		
		// Physics
		lua << "            gravity = {" << config.gravity.x << ", " << config.gravity.y << ", " << config.gravity.z << "},\n";
		lua << "            drag = " << config.drag << ",\n";
		
		// Texture
		lua << "            texturePath = \"" << config.texturePath << "\",\n";
		lua << "            useTexture = " << (config.useTexture ? "true" : "false") << ",\n";
		lua << "            additiveBlending = " << (config.additiveBlending ? "true" : "false") << ",\n";
		
		lua << "            maxParticles = " << config.maxParticles << ",\n";
		lua << "        },\n";
	}
	
	lua << "    },\n";
	lua << "}\n";
	
	// Write to file
	std::ofstream file(luaPath);
	if (!file.is_open()) {
		TOAST_ERROR("Failed to save particle system config to: {}", luaPath);
		return false;
	}
	
	file << lua.str();
	file.close();
	
	TOAST_INFO("Saved particle system config to: {}", luaPath);
	return true;
}

void ParticleSystem::InitSharedResources() {
	if (m_sharedResourcesInitialized) return;
	
	// Load shared shaders
	m_computeShader = resource::ResourceManager::GetInstance()->LoadResource<renderer::Shader>("shaders/particles_compute.shader");
	m_renderShader = resource::ResourceManager::GetInstance()->LoadResource<renderer::Shader>("shaders/particles_render.shader");
	
	// Create shared quad VAO/VBO
	float quadVertices[] = {
		-0.5f, -0.5f,
		 0.5f, -0.5f,
		 0.5f,  0.5f,
		-0.5f, -0.5f,
		 0.5f,  0.5f,
		-0.5f,  0.5f
	};
	
	glGenVertexArrays(1, &m_quadVAO);
	glGenBuffers(1, &m_quadVBO);
	
	glBindVertexArray(m_quadVAO);
	glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
	
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
	
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	m_sharedResourcesInitialized = true;
	
	// Initialize GPU resources for all emitters
	for (auto& emitter : m_emitters) {
		emitter.InitGPUResources(m_computeShader, m_renderShader);
	}
}

void ParticleSystem::CleanupSharedResources() {
	if (!m_sharedResourcesInitialized) return;
	
	if (m_quadVAO) {
		glDeleteVertexArrays(1, &m_quadVAO);
		m_quadVAO = 0;
	}
	if (m_quadVBO) {
		glDeleteBuffers(1, &m_quadVBO);
		m_quadVBO = 0;
	}
	
	m_computeShader.reset();
	m_renderShader.reset();
	
	m_sharedResourcesInitialized = false;
}

void ParticleSystem::Play() {
	m_isPlaying = true;
	for (auto& emitter : m_emitters) {
		emitter.Play();
	}
}

void ParticleSystem::Pause() {
	m_isPlaying = false;
	for (auto& emitter : m_emitters) {
		emitter.Pause();
	}
}

void ParticleSystem::Stop() {
	m_isPlaying = false;
	for (auto& emitter : m_emitters) {
		emitter.Stop();
	}
}

void ParticleSystem::EmitBurst(uint32_t count) {
	const glm::vec3 worldPos = worldPosition();
	for (auto& emitter : m_emitters) {
		emitter.EmitBurst(count, worldPos + emitter.GetConfig().localOffset);
	}
}

uint32_t ParticleSystem::GetParticleCount() const {
	uint32_t total = 0;
	for (const auto& emitter : m_emitters) {
		total += emitter.GetParticleCount();
	}
	return total;
}

ParticleEmitter& ParticleSystem::AddEmitter() {
	m_emitters.emplace_back();
	auto& emitter = m_emitters.back();
	emitter.GetConfig().name = "Emitter " + std::to_string(m_emitters.size());
	
	if (m_sharedResourcesInitialized) {
		emitter.InitGPUResources(m_computeShader, m_renderShader);
	}
	
	return emitter;
}

ParticleEmitter& ParticleSystem::AddEmitterWithPreset(const std::string& presetName) {
	auto& emitter = AddEmitter();
	emitter.GetConfig().ApplyPreset(presetName);
	emitter.GetConfig().name = presetName;
	return emitter;
}

void ParticleSystem::RemoveEmitter(size_t index) {
	if (index < m_emitters.size()) {
		m_emitters[index].CleanupGPUResources();
		m_emitters.erase(m_emitters.begin() + index);
	}
}

#ifdef TOAST_EDITOR
void ParticleSystem::Inspector() {
	TransformComponent::Inspector();
	
	ImGui::DragInt("Culling Radius", &m_cullingRadius, 1, 1, 1000);
	
	ImGui::Separator();
	ImGui::Text("Particle System (%zu emitters)", m_emitters.size());
	ImGui::Separator();
	
	// Lua config path
	char luaPath[256];
	strncpy(luaPath, m_luaConfigPath.c_str(), sizeof(luaPath) - 1);
	luaPath[sizeof(luaPath) - 1] = '\0';
	if (ImGui::InputText("Lua Config Path", luaPath, sizeof(luaPath))) {
		m_luaConfigPath = luaPath;
	}
	ImGui::SameLine();
	if (ImGui::Button("Load")) {
		LoadFromLua(m_luaConfigPath);
	}
	ImGui::SameLine();
	if (ImGui::Button("Save")) {
		SaveToLua(m_luaConfigPath);
	}
	
	ImGui::Separator();
	
	// Global playback controls
	ImGui::Text("Playback");
	ImGui::SameLine();
	if (ImGui::Button(m_isPlaying ? "Pause" : "Play")) {
		if (m_isPlaying) Pause();
		else Play();
	}
	ImGui::SameLine();
	if (ImGui::Button("Stop")) {
		Stop();
	}
	ImGui::SameLine();
	if (ImGui::Button("Emit 10")) {
		EmitBurst(10);
	}
	
	ImGui::Text("Total Particles: %u", GetParticleCount());
	
	ImGui::Separator();
	
	// Emitter management
	if (ImGui::Button("Add Emitter")) {
		AddEmitter();
	}
	ImGui::SameLine();
	if (ImGui::Button("Add Smoke")) { AddEmitterWithPreset("Smoke"); }
	ImGui::SameLine();
	if (ImGui::Button("Add Fire")) { AddEmitterWithPreset("Fire"); }
	ImGui::SameLine();
	if (ImGui::Button("Add Sparks")) { AddEmitterWithPreset("Sparks"); }
	
	ImGui::Separator();
	
	// Per-emitter UI
	int emitterToRemove = -1;
	for (size_t i = 0; i < m_emitters.size(); ++i) {
		auto& emitter = m_emitters[i];
		auto& config = emitter.GetConfig();
		
		ImGui::PushID(static_cast<int>(i));
		
		std::string headerLabel = config.name + " (" + std::to_string(emitter.GetParticleCount()) + " particles)";
		bool open = ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
		
		if (open) {
			ImGui::Indent(10);
			
			// Emitter name and enable
			char nameBuf[128];
			strncpy(nameBuf, config.name.c_str(), sizeof(nameBuf) - 1);
			nameBuf[sizeof(nameBuf) - 1] = '\0';
			if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
				config.name = nameBuf;
			}
			
			ImGui::Checkbox("Enabled", &config.enabled);
			ImGui::SameLine();
			if (ImGui::Button("Remove")) {
				emitterToRemove = static_cast<int>(i);
			}
			
			// Emission
			if (ImGui::TreeNode("Emission")) {
				const char* emissionModes[] = { "Continuous", "Burst" };
				int currentMode = static_cast<int>(config.emissionMode);
				if (ImGui::Combo("Mode", &currentMode, emissionModes, 2)) {
					config.emissionMode = static_cast<EmissionMode>(currentMode);
				}
				
				if (config.emissionMode == EmissionMode::Continuous) {
					ImGui::DragFloat("Rate", &config.emissionRate, 0.5f, 0.0f, 1000.0f, "%.1f/s");
				}
				
				ImGui::DragFloat3("Offset", &config.localOffset.x, 0.1f);
				
				int maxP = static_cast<int>(config.maxParticles);
				if (ImGui::DragInt("Max Particles", &maxP, 100, 100, 100000)) {
					config.maxParticles = static_cast<uint32_t>(maxP);
				}
				
				ImGui::TreePop();
			}
			
			// Shape
			if (ImGui::TreeNode("Shape")) {
				const char* shapes[] = { "Point", "Sphere", "Box", "Cone" };
				int currentShape = static_cast<int>(config.shape);
				if (ImGui::Combo("Shape", &currentShape, shapes, 4)) {
					config.shape = static_cast<EmitterShape>(currentShape);
				}
				
				switch (config.shape) {
					case EmitterShape::Sphere:
						ImGui::DragFloat("Radius", &config.shapeSize.x, 0.1f, 0.0f, 100.0f);
						break;
					case EmitterShape::Box:
						ImGui::DragFloat3("Size", &config.shapeSize.x, 0.1f, 0.0f, 100.0f);
						break;
					case EmitterShape::Cone:
						ImGui::DragFloat("Angle", &config.coneAngle, 1.0f, 0.0f, 90.0f);
						break;
					default:
						break;
				}
				ImGui::TreePop();
			}
			
			// Lifetime
			if (ImGui::TreeNode("Lifetime")) {
				ImGui::DragFloatRange2("Lifetime", &config.lifetime.min, &config.lifetime.max, 0.1f, 0.01f, 60.0f);
				ImGui::TreePop();
			}
			
			// Velocity
			if (ImGui::TreeNode("Velocity")) {
				ImGui::DragFloatRange2("Speed", &config.speed.min, &config.speed.max, 0.1f, 0.0f, 100.0f);
				ImGui::DragFloat3("Direction", &config.direction.x, 0.1f, -1.0f, 1.0f);
				ImGui::DragFloat("Randomness", &config.directionRandomness, 0.01f, 0.0f, 1.0f);
				ImGui::TreePop();
			}
			
			// Size
			if (ImGui::TreeNode("Size")) {
				ImGui::DragFloatRange2("Start", &config.startSize.min, &config.startSize.max, 0.05f, 0.01f, 50.0f);
				ImGui::DragFloatRange2("End", &config.endSize.min, &config.endSize.max, 0.05f, 0.01f, 50.0f);
				ImGui::TreePop();
			}
			
			// Color
			if (ImGui::TreeNode("Color")) {
				ImGui::ColorEdit4("Start", &config.startColor.r);
				ImGui::ColorEdit4("End", &config.endColor.r);
				ImGui::TreePop();
			}
			
			// Physics
			if (ImGui::TreeNode("Physics")) {
				ImGui::DragFloat3("Gravity", &config.gravity.x, 0.1f, -100.0f, 100.0f);
				ImGui::DragFloat("Drag", &config.drag, 0.01f, 0.0f, 10.0f);
				ImGui::TreePop();
			}
			
			// Rendering
			if (ImGui::TreeNode("Rendering")) {
				ImGui::Checkbox("Use Texture", &config.useTexture);
				if (config.useTexture) {
					char texPath[256];
					strncpy(texPath, config.texturePath.c_str(), sizeof(texPath) - 1);
					texPath[sizeof(texPath) - 1] = '\0';
					if (ImGui::InputText("Texture", texPath, sizeof(texPath))) {
						config.texturePath = texPath;
					}
					if (ImGui::Button("Reload Texture")) {
						emitter.LoadTexture();
					}
				}
				ImGui::Checkbox("Additive", &config.additiveBlending);
				ImGui::TreePop();
			}
			
			// Presets
			if (ImGui::TreeNode("Apply Preset")) {
				if (ImGui::Button("Smoke")) { config.ApplyPreset("Smoke"); }
				ImGui::SameLine();
				if (ImGui::Button("Fire")) { config.ApplyPreset("Fire"); }
				ImGui::SameLine();
				if (ImGui::Button("Sparks")) { config.ApplyPreset("Sparks"); }
				if (ImGui::Button("Snow")) { config.ApplyPreset("Snow"); }
				ImGui::SameLine();
				if (ImGui::Button("Explosion")) { config.ApplyPreset("Explosion"); }
				ImGui::TreePop();
			}
			
			ImGui::Unindent(10);
		}
		
		ImGui::PopID();
	}
	
	if (emitterToRemove >= 0) {
		RemoveEmitter(static_cast<size_t>(emitterToRemove));
	}
}
#endif

}
