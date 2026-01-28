/// @file ParticleSystem.cpp
/// @date 01/20/2026
/// @brief compute Particle System

#include "Toast/Objects/ParticleSystem.hpp"

#include "Toast/Log.hpp"
#include "Toast/Profiler.hpp"
#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Renderer/OclussionVolume.hpp"
#include "Toast/Resources/ResourceManager.hpp"
#include "Toast/Time.hpp"

#include <glad/gl.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

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
	return glm::vec3(t[1].get_or<float>(defaultVal.x), t[2].get_or<float>(defaultVal.y), t[3].get_or<float>(defaultVal.z));
}

static glm::vec4 ParseVec4FromLua(const sol::object& obj, const glm::vec4& defaultVal = glm::vec4(1.0f)) {
	if (!obj.valid() || !obj.is<sol::table>()) {
		return defaultVal;
	}
	sol::table t = obj.as<sol::table>();
	return glm::vec4(
	    t[1].get_or<float>(defaultVal.x), t[2].get_or<float>(defaultVal.y), t[3].get_or<float>(defaultVal.z), t[4].get_or<float>(defaultVal.w)
	);
}

static EmissionMode ParseEmissionMode(const std::string& mode) {
	if (mode == "burst" || mode == "Burst") {
		return EmissionMode::Burst;
	}
	return EmissionMode::Continuous;
}

static EmitterShape ParseEmitterShape(const std::string& shape) {
	if (shape == "sphere" || shape == "Sphere") {
		return EmitterShape::Sphere;
	}
	if (shape == "box" || shape == "Box") {
		return EmitterShape::Box;
	}
	if (shape == "cone" || shape == "Cone") {
		return EmitterShape::Cone;
	}
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

	// Looping and duration
	sol::optional<bool> loopingOpt = table["looping"];
	looping = loopingOpt.value_or(true);
	sol::optional<float> durationOpt = table["duration"];
	duration = durationOpt.value_or(5.0f);

	// Shape
	sol::optional<std::string> shapeStr = table["shape"];
	if (shapeStr) {
		shape = ParseEmitterShape(*shapeStr);
	}
	shapeSize = ParseVec3FromLua(table["shapeSize"], glm::vec3(1.0f));
	sol::optional<float> coneAngleOpt = table["coneAngle"];
	coneAngle = coneAngleOpt.value_or(45.0f);

	// Offset and rotation
	localOffset = ParseVec3FromLua(table["localOffset"], glm::vec3(0.0f));
	localRotation = ParseVec3FromLua(table["localRotation"], glm::vec3(0.0f));

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

	// Max particles (clamped to limit)
	sol::optional<uint32_t> maxParticlesOpt = table["maxParticles"];
	maxParticles = std::min(maxParticlesOpt.value_or(10000u), MAX_PARTICLES_LIMIT);
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
		bursts = {
			{ 0.0f, 50, 0.5f, false }
		};
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
		bursts = {
			{ 0.0f, 100, 0.0f, false }
		};
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

ParticleEmitter::ParticleEmitter() : m_rng(std::random_device {}()) { }

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

void ParticleEmitter::InitGPUResources(
    const std::shared_ptr<renderer::Shader>& computeShader, const std::shared_ptr<renderer::Shader>& renderShader
) {
	if (m_gpuInitialized) {
		return;
	}

	m_computeShader = computeShader;
	m_renderShader = renderShader;

	LoadTexture();

	// Clamp max particles
	m_config.maxParticles = std::min(m_config.maxParticles, ParticleEmitterConfig::MAX_PARTICLES_LIMIT);
	if (m_config.maxParticles < 100) {
		m_config.maxParticles = 100;
	}

	// Create double buffered particle SSBOs
	size_t bufferSize = sizeof(GPUParticle) * m_config.maxParticles;

	const size_t maxBufferSize = 250 * 1024 * 1024;    // 250MB per buffer
	if (bufferSize > maxBufferSize) {
		m_config.maxParticles = static_cast<uint32_t>(maxBufferSize / sizeof(GPUParticle));
		bufferSize = sizeof(GPUParticle) * m_config.maxParticles;
		TOAST_WARN("Particle buffer size capped to {} particles ({} bytes)", m_config.maxParticles, bufferSize);
	}

	glGenBuffers(2, m_particleBuffers);
	for (int i = 0; i < 2; ++i) {
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_particleBuffers[i]);
		glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, nullptr, GL_DYNAMIC_DRAW);
	}

	// persistent mapping counter
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
	if (!m_gpuInitialized) {
		return;
	}

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

void ParticleEmitter::ReinitializeBuffers() {
	if (!m_gpuInitialized) {
		return;
	}

	// Store current shaders
	auto computeShader = m_computeShader;
	auto renderShader = m_renderShader;

	// Cleanup
	CleanupGPUResources();
	InitGPUResources(computeShader, renderShader);

	// Reset state
	m_aliveCount = 0;
	m_systemTime = 0.0f;
	m_emissionAccumulator = 0.0f;
	for (auto& burst : m_config.bursts) {
		burst.triggered = false;
	}
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

void ParticleEmitter::UpdateAndRender(
    const glm::mat4& viewProjection, const glm::vec3& worldPos, const glm::mat3& parentRotation, const glm::vec3& camRight, const glm::vec3& camUp,
    float deltaTime
) {
	if (!m_gpuInitialized || !m_computeShader || !m_renderShader || !m_config.enabled) {
		return;
	}

	// combined rotation
	glm::mat3 localRot = glm::mat3(
	    glm::eulerAngleYXZ(glm::radians(m_config.localRotation.y), glm::radians(m_config.localRotation.x), glm::radians(m_config.localRotation.z))
	);
	glm::mat3 combinedRotation = parentRotation * localRot;

	// Transform local offset by parent rotation
	glm::vec3 transformedOffset = parentRotation * m_config.localOffset;
	glm::vec3 emitterWorldPos = worldPos + transformedOffset;

	if (m_isPlaying) {
		m_systemTime += deltaTime;

		// Check if non-looping emitter has finished
		bool canEmit = true;
		if (!m_config.looping && m_systemTime >= m_config.duration) {
			canEmit = false;
			// Auto-stop
			if (m_aliveCount == 0) {
				m_isPlaying = false;
			}
		}

		if (canEmit) {
			if (m_config.emissionMode == EmissionMode::Continuous) {
				m_emissionAccumulator += m_config.emissionRate * deltaTime;
				const uint32_t toSpawn = static_cast<uint32_t>(m_emissionAccumulator);
				if (toSpawn > 0) {
					SpawnParticles(toSpawn, emitterWorldPos, combinedRotation);
					m_emissionAccumulator -= static_cast<float>(toSpawn);
				}
			}

			for (auto& burst : m_config.bursts) {
				if (!burst.triggered && m_systemTime >= burst.time) {
					SpawnParticles(burst.count, emitterWorldPos, combinedRotation);
					burst.triggered = true;
				}
				// Only repeat if looping or has duration
				if (burst.cycleInterval > 0.0f && burst.triggered && m_config.looping) {
					const float cycleTime = fmod(m_systemTime - burst.time, burst.cycleInterval);
					if (cycleTime < deltaTime) {
						SpawnParticles(burst.count, emitterWorldPos, combinedRotation);
					}
				}
			}
		}
	}

	if (m_aliveCount == 0) {
		return;
	}

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

	if (m_aliveCount == 0) {
		return;
	}

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

	// Set texture usage flag
	int useTexture = (m_texture && m_config.useTexture) ? 1 : 0;
	m_renderShader->Set("u_UseTexture", useTexture);

	if (m_texture && m_config.useTexture) {
		m_texture->Bind(1);
		m_renderShader->SetSampler("u_Tex", 1);
	}

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_particleBuffers[m_currentBuffer]);
}

void ParticleEmitter::SpawnParticles(uint32_t count, const glm::vec3& worldPos, const glm::mat3& parentRotation) {
	if (!m_gpuInitialized || count == 0) {
		return;
	}

	const uint32_t available = m_config.maxParticles - m_aliveCount;
	count = std::min(count, available);

	if (count == 0) {
		return;
	}

	std::vector<GPUParticle> newParticles(count);

	for (uint32_t i = 0; i < count; ++i) {
		GPUParticle& p = newParticles[i];

		// Generate spawn offset and transform by rotation
		glm::vec3 spawnOffset = GenerateSpawnPosition(parentRotation);
		glm::vec3 pos = worldPos + spawnOffset;

		float startSize = RandomFloat(m_config.startSize.min, m_config.startSize.max);
		float endSize = RandomFloat(m_config.endSize.min, m_config.endSize.max);

		p.pos = glm::vec4(pos, startSize);

		// Generate velocity
		glm::vec3 vel = GenerateSpawnVelocity(parentRotation);
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

glm::vec3 ParticleEmitter::GenerateSpawnPosition(const glm::mat3& rotation) {
	glm::vec3 localPos;
	switch (m_config.shape) {
		case EmitterShape::Sphere: {
			glm::vec3 p;
			do {
				p = glm::vec3(RandomFloat(-1.0f, 1.0f), RandomFloat(-1.0f, 1.0f), RandomFloat(-1.0f, 1.0f));
			} while (glm::dot(p, p) > 1.0f);
			localPos = p * m_config.shapeSize.x;
			break;
		}

		case EmitterShape::Box:
			localPos = glm::vec3(
			               RandomFloat(-m_config.shapeSize.x, m_config.shapeSize.x),
			               RandomFloat(-m_config.shapeSize.y, m_config.shapeSize.y),
			               RandomFloat(-m_config.shapeSize.z, m_config.shapeSize.z)
			           ) *
			           0.5f;
			break;

		case EmitterShape::Point:
		case EmitterShape::Cone:
		default: localPos = glm::vec3(0.0f); break;
	}

	// Transform by rotation
	return rotation * localPos;
}

glm::vec3 ParticleEmitter::GenerateSpawnVelocity(const glm::mat3& rotation) {
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

	dir = rotation * dir;

	return dir * speed;
}

float ParticleEmitter::RandomFloat(float min, float max) {
	return min + m_dist(m_rng) * (max - min);
}

glm::vec3 ParticleEmitter::RandomDirection() {
	const float theta = RandomFloat(0.0f, 2.0f * glm::pi<float>());
	const float phi = std::acos(RandomFloat(-1.0f, 1.0f));

	return glm::vec3(std::sin(phi) * std::cos(theta), std::sin(phi) * std::sin(theta), std::cos(phi));
}

void ParticleSystem::OnRender(const glm::mat4& viewProjection) noexcept {
	if (!m_sharedResourcesInitialized) {
		return;
	}

	if (!OclussionVolume::isSphereOnPlanes(renderer::IRendererBase::GetInstance()->GetFrustumPlanes(), worldPosition(), m_cullingRadius)) {
		return;
	}

	PROFILE_ZONE;

	const float dt = static_cast<float>(Time::delta());
	const glm::vec3 worldPos = worldPosition();

	glm::mat4 viewMatrix = renderer::IRendererBase::GetInstance()->GetViewMatrix();
	glm::vec3 camRight = glm::vec3(viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0]);
	glm::vec3 camUp = glm::vec3(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);
	glm::vec3 camPos = glm::vec3(glm::inverse(viewMatrix)[3]);

	// Get parent rotation from transform
	glm::mat3 parentRotation = glm::mat3_cast(worldRotationQuat());

	// Sort emitters by distance to camera
	std::vector<size_t> emitterOrder(m_emitters.size());
	for (size_t i = 0; i < emitterOrder.size(); ++i) {
		emitterOrder[i] = i;
	}
	std::sort(emitterOrder.begin(), emitterOrder.end(), [&](size_t a, size_t b) {
		glm::vec3 posA = worldPos + parentRotation * m_emitters[a].GetConfig().localOffset;
		glm::vec3 posB = worldPos + parentRotation * m_emitters[b].GetConfig().localOffset;
		float distA = glm::distance(camPos, posA);
		float distB = glm::distance(camPos, posB);
		return distA > distB;    // Back to front
	});

	// Bind shared quad VAO once
	glBindVertexArray(m_quadVAO);

	for (size_t idx : emitterOrder) {
		auto& emitter = m_emitters[idx];
		if (!emitter.IsGPUInitialized()) {
			continue;
		}

		emitter.UpdateAndRender(viewProjection, worldPos, parentRotation, camRight, camUp, dt);

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

	if (j.contains("playing")) {
		bool playing = j["playing"].get<bool>();
		if (playing) {
			Play();
		} else {
			Stop();
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
	j["playing"] = m_isPlaying;
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

					// Reinitialize with new config
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
	sol::state lua;
	lua.open_libraries(sol::lib::base, sol::lib::table);

	// Create the main config table
	sol::table config = lua.create_table();
	config["format"] = "particle_system";

	// Create emitters table
	sol::table emittersTable = lua.create_table();

	for (size_t i = 0; i < m_emitters.size(); ++i) {
		const auto& emitterConfig = m_emitters[i].GetConfig();
		sol::table emitterTable = lua.create_table();

		// Identification
		emitterTable["name"] = emitterConfig.name;
		emitterTable["enabled"] = emitterConfig.enabled;

		// Emission
		emitterTable["emissionMode"] = (emitterConfig.emissionMode == EmissionMode::Burst) ? "burst" : "continuous";
		emitterTable["emissionRate"] = emitterConfig.emissionRate;
		emitterTable["looping"] = emitterConfig.looping;
		emitterTable["duration"] = emitterConfig.duration;

		// Bursts
		if (!emitterConfig.bursts.empty()) {
			sol::table burstsTable = lua.create_table();
			for (size_t j = 0; j < emitterConfig.bursts.size(); ++j) {
				sol::table burstTable = lua.create_table();
				burstTable["time"] = emitterConfig.bursts[j].time;
				burstTable["count"] = emitterConfig.bursts[j].count;
				burstTable["cycleInterval"] = emitterConfig.bursts[j].cycleInterval;
				burstsTable[j + 1] = burstTable;
			}
			emitterTable["bursts"] = burstsTable;
		}

		// Shape
		std::string shapeStr = "point";
		if (emitterConfig.shape == EmitterShape::Sphere) {
			shapeStr = "sphere";
		} else if (emitterConfig.shape == EmitterShape::Box) {
			shapeStr = "box";
		} else if (emitterConfig.shape == EmitterShape::Cone) {
			shapeStr = "cone";
		}
		emitterTable["shape"] = shapeStr;
		emitterTable["shapeSize"] = lua.create_table_with(1, emitterConfig.shapeSize.x, 2, emitterConfig.shapeSize.y, 3, emitterConfig.shapeSize.z);
		emitterTable["coneAngle"] = emitterConfig.coneAngle;

		// Transform
		emitterTable["localOffset"] =
		    lua.create_table_with(1, emitterConfig.localOffset.x, 2, emitterConfig.localOffset.y, 3, emitterConfig.localOffset.z);
		emitterTable["localRotation"] =
		    lua.create_table_with(1, emitterConfig.localRotation.x, 2, emitterConfig.localRotation.y, 3, emitterConfig.localRotation.z);

		// Lifetime/Speed
		emitterTable["lifetime"] = lua.create_table_with(1, emitterConfig.lifetime.min, 2, emitterConfig.lifetime.max);
		emitterTable["speed"] = lua.create_table_with(1, emitterConfig.speed.min, 2, emitterConfig.speed.max);
		emitterTable["direction"] = lua.create_table_with(1, emitterConfig.direction.x, 2, emitterConfig.direction.y, 3, emitterConfig.direction.z);
		emitterTable["directionRandomness"] = emitterConfig.directionRandomness;

		// Size
		emitterTable["startSize"] = lua.create_table_with(1, emitterConfig.startSize.min, 2, emitterConfig.startSize.max);
		emitterTable["endSize"] = lua.create_table_with(1, emitterConfig.endSize.min, 2, emitterConfig.endSize.max);

		// Rotation
		emitterTable["startRotation"] = lua.create_table_with(1, emitterConfig.startRotation.min, 2, emitterConfig.startRotation.max);
		emitterTable["rotationSpeed"] = lua.create_table_with(1, emitterConfig.rotationSpeed.min, 2, emitterConfig.rotationSpeed.max);

		// Color
		emitterTable["startColor"] = lua.create_table_with(
		    1, emitterConfig.startColor.r, 2, emitterConfig.startColor.g, 3, emitterConfig.startColor.b, 4, emitterConfig.startColor.a
		);
		emitterTable["endColor"] =
		    lua.create_table_with(1, emitterConfig.endColor.r, 2, emitterConfig.endColor.g, 3, emitterConfig.endColor.b, 4, emitterConfig.endColor.a);
		emitterTable["randomizeStartColor"] = emitterConfig.randomizeStartColor;
		emitterTable["startColorRangeMin"] = lua.create_table_with(
		    1,
		    emitterConfig.startColorRangeMin.r,
		    2,
		    emitterConfig.startColorRangeMin.g,
		    3,
		    emitterConfig.startColorRangeMin.b,
		    4,
		    emitterConfig.startColorRangeMin.a
		);
		emitterTable["startColorRangeMax"] = lua.create_table_with(
		    1,
		    emitterConfig.startColorRangeMax.r,
		    2,
		    emitterConfig.startColorRangeMax.g,
		    3,
		    emitterConfig.startColorRangeMax.b,
		    4,
		    emitterConfig.startColorRangeMax.a
		);

		// Physics
		emitterTable["gravity"] = lua.create_table_with(1, emitterConfig.gravity.x, 2, emitterConfig.gravity.y, 3, emitterConfig.gravity.z);
		emitterTable["drag"] = emitterConfig.drag;

		// Texture
		emitterTable["texturePath"] = emitterConfig.texturePath;
		emitterTable["useTexture"] = emitterConfig.useTexture;
		emitterTable["additiveBlending"] = emitterConfig.additiveBlending;

		// Max particles
		emitterTable["maxParticles"] = emitterConfig.maxParticles;

		emittersTable[i + 1] = emitterTable;
	}

	config["emitters"] = emittersTable;

	// Helper function
	auto serializeTable = [](sol::state& lua, const sol::table& table, int indent = 0) -> std::string {
		std::function<std::string(const sol::table&, int)> serialize = [&](const sol::table& t, int ind) -> std::string {
			std::ostringstream ss;
			std::string indentStr(ind * 4, ' ');
			std::string indentStrInner((ind + 1) * 4, ' ');

			ss << "{\n";

			// Collect keys to determine if table is array-like
			bool isArray = true;
			size_t expectedIndex = 1;
			std::vector<std::pair<sol::object, sol::object>> pairs;
			for (auto& kv : t) {
				pairs.push_back({ kv.first, kv.second });
				if (!kv.first.is<size_t>() || kv.first.as<size_t>() != expectedIndex) {
					isArray = false;
				}
				expectedIndex++;
			}

			for (size_t i = 0; i < pairs.size(); ++i) {
				auto& [key, value] = pairs[i];

				ss << indentStrInner;

				// Write key
				if (!isArray) {
					if (key.is<std::string>()) {
						ss << key.as<std::string>() << " = ";
					} else if (key.is<int>()) {
						ss << "[" << key.as<int>() << "] = ";
					}
				}

				// Write value
				if (value.is<sol::table>()) {
					ss << serialize(value.as<sol::table>(), ind + 1);
				} else if (value.is<std::string>()) {
					ss << "\"" << value.as<std::string>() << "\"";
				} else if (value.is<bool>()) {
					ss << (value.as<bool>() ? "true" : "false");
				} else if (value.is<double>()) {
					ss << value.as<double>();
				} else if (value.is<int>()) {
					ss << value.as<int>();
				}

				ss << ",\n";
			}

			ss << indentStr << "}";
			return ss.str();
		};

		return serialize(table, indent);
	};

	// Generate Lua file content
	std::ostringstream luaFile;
	luaFile << "-- Particle System Configuration\n\n";
	luaFile << "return " << serializeTable(lua, config, 0) << "\n";

	// Write to file
	std::ofstream file(".\\assets\\" + luaPath);
	if (!file.is_open()) {
		TOAST_ERROR("Failed to save particle system config to: {}", luaPath);
		return false;
	}

	file << luaFile.str();
	file.close();

	TOAST_INFO("Saved particle system config to: {}", luaPath);
	return true;
}

void ParticleSystem::InitSharedResources() {
	if (m_sharedResourcesInitialized) {
		return;
	}

	// Load shared shaders
	m_computeShader = resource::ResourceManager::GetInstance()->LoadResource<renderer::Shader>("shaders/particles_compute.shader");
	m_renderShader = resource::ResourceManager::GetInstance()->LoadResource<renderer::Shader>("shaders/particles_render.shader");

	// Create shared quad
	float quadVertices[] = { -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f };

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

	// Initialize GPU resources
	for (auto& emitter : m_emitters) {
		emitter.InitGPUResources(m_computeShader, m_renderShader);
	}
}

void ParticleSystem::CleanupSharedResources() {
	if (!m_sharedResourcesInitialized) {
		return;
	}

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
	glm::mat3 parentRotation = glm::mat3_cast(worldRotationQuat());
	for (auto& emitter : m_emitters) {
		glm::vec3 transformedOffset = parentRotation * emitter.GetConfig().localOffset;
		emitter.EmitBurst(count, worldPos + transformedOffset, parentRotation);
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
	ImGui::InputText("Lua Config name", &m_luaConfigPath);
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
		if (m_isPlaying) {
			Pause();
		} else {
			Play();
		}
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
	if (ImGui::Button("Add Smoke")) {
		AddEmitterWithPreset("Smoke");
	}
	ImGui::SameLine();
	if (ImGui::Button("Add Fire")) {
		AddEmitterWithPreset("Fire");
	}
	ImGui::SameLine();
	if (ImGui::Button("Add Sparks")) {
		AddEmitterWithPreset("Sparks");
	}

	ImGui::Separator();

	// Per-emitter UI
	int emitterToRemove = -1;
	for (size_t i = 0; i < m_emitters.size(); ++i) {
		auto& emitter = m_emitters[i];
		auto& config = emitter.GetConfig();

		ImGui::PushID(static_cast<int>(i));

		std::string headerLabel = config.name;
		bool open = ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

		ImGui::SameLine();
		ImGui::TextDisabled("(%u particles)", emitter.GetParticleCount());

		if (open) {
			ImGui::Indent(10);

			// Emitter name and enable
			ImGui::InputText("Name", &config.name);

			ImGui::Checkbox("Enabled", &config.enabled);
			ImGui::SameLine();
			if (ImGui::Button("Remove")) {
				emitterToRemove = static_cast<int>(i);
			}

			// Transform
			if (ImGui::TreeNode("Transform")) {
				ImGui::DragFloat3("Offset", &config.localOffset.x, 0.1f);
				ImGui::DragFloat3("Rotation", &config.localRotation.x, 1.0f, -180.0f, 180.0f);
				ImGui::TreePop();
			}

			// Emission
			if (ImGui::TreeNode("Emission")) {
				const char* emissionModes[] = { "Continuous", "Burst" };
				int currentMode = static_cast<int>(config.emissionMode);
				if (ImGui::Combo("Mode", &currentMode, emissionModes, 2)) {
					config.emissionMode = static_cast<EmissionMode>(currentMode);
				}

				// Looping and duration
				ImGui::Checkbox("Looping", &config.looping);
				if (!config.looping) {
					ImGui::DragFloat("Duration", &config.duration, 0.1f, 0.1f, 60.0f, "%.1f s");
				}

				if (config.emissionMode == EmissionMode::Continuous) {
					ImGui::DragFloat("Rate", &config.emissionRate, 0.5f, 0.0f, 1000.0f, "%.1f/s");
				}

				// Burst configuration
				if (ImGui::TreeNode("Bursts")) {
					// Add burst button
					if (ImGui::Button("Add Burst")) {
						config.bursts.push_back({ 0.0f, 10, 0.0f, false });
					}

					int burstToRemove = -1;
					for (size_t b = 0; b < config.bursts.size(); ++b) {
						ImGui::PushID(static_cast<int>(b));

						ImGui::Separator();
						ImGui::Text("Burst %zu", b + 1);

						ImGui::DragFloat("Time", &config.bursts[b].time, 0.1f, 0.0f, 60.0f, "%.2f s");

						int burstCount = static_cast<int>(config.bursts[b].count);
						if (ImGui::DragInt("Count", &burstCount, 1, 1, 10000)) {
							config.bursts[b].count = static_cast<uint32_t>(std::max(1, burstCount));
						}

						ImGui::DragFloat("Repeat Interval", &config.bursts[b].cycleInterval, 0.1f, 0.0f, 60.0f, "%.2f s");
						if (config.bursts[b].cycleInterval > 0.0f) {
							ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.3f, 1.0f), "Repeats every %.2f s", config.bursts[b].cycleInterval);
						}

						if (ImGui::Button("Remove")) {
							burstToRemove = static_cast<int>(b);
						}
						ImGui::SameLine();
						if (ImGui::Button("Trigger Now")) {
							config.bursts[b].triggered = false;
						}

						ImGui::PopID();
					}

					if (burstToRemove >= 0) {
						config.bursts.erase(config.bursts.begin() + burstToRemove);
					}

					ImGui::TreePop();
				}

				// Max particles with validation
				int maxP = static_cast<int>(config.maxParticles);
				if (ImGui::DragInt("Max Particles", &maxP, 100, 100, static_cast<int>(ParticleEmitterConfig::MAX_PARTICLES_LIMIT))) {
					config.maxParticles = static_cast<uint32_t>(std::clamp(maxP, 100, static_cast<int>(ParticleEmitterConfig::MAX_PARTICLES_LIMIT)));
				}
				if (config.maxParticles > 50000) {
					ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Warning: High particle count may affect performance");
				}
				ImGui::SameLine();
				if (ImGui::Button("Apply##maxparticles")) {
					emitter.ReinitializeBuffers();
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Reinitialize GPU buffers with new max particles value.\nThis will clear all current particles.");
				}

				// Playback controls for this emitter
				ImGui::Separator();
				ImGui::Text("Emitter Playback");
				if (ImGui::Button(emitter.IsPlaying() ? "Pause##emitter" : "Play##emitter")) {
					if (emitter.IsPlaying()) {
						emitter.Pause();
					} else {
						emitter.Play();
					}
				}
				ImGui::SameLine();
				if (ImGui::Button("Stop##emitter")) {
					emitter.Stop();
				}
				ImGui::SameLine();
				if (ImGui::Button("Restart##emitter")) {
					emitter.Stop();
					emitter.Play();
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
					case EmitterShape::Sphere: ImGui::DragFloat("Radius", &config.shapeSize.x, 0.1f, 0.0f, 100.0f); break;
					case EmitterShape::Box: ImGui::DragFloat3("Size", &config.shapeSize.x, 0.1f, 0.0f, 100.0f); break;
					case EmitterShape::Cone: ImGui::DragFloat("Angle", &config.coneAngle, 1.0f, 0.0f, 90.0f); break;
					default: break;
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

			// Rotation
			if (ImGui::TreeNode("Particle Rotation")) {
				ImGui::DragFloatRange2("Start Rotation", &config.startRotation.min, &config.startRotation.max, 1.0f, 0.0f, 360.0f);
				ImGui::DragFloatRange2("Rotation Speed", &config.rotationSpeed.min, &config.rotationSpeed.max, 1.0f, -360.0f, 360.0f);
				ImGui::TreePop();
			}

			// Color
			if (ImGui::TreeNode("Color")) {
				ImGui::ColorEdit4("Start", &config.startColor.r);
				ImGui::ColorEdit4("End", &config.endColor.r);
				ImGui::Checkbox("Randomize Start Color", &config.randomizeStartColor);
				if (config.randomizeStartColor) {
					ImGui::ColorEdit4("Random Min", &config.startColorRangeMin.r);
					ImGui::ColorEdit4("Random Max", &config.startColorRangeMax.r);
				}
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
					ImGui::InputText("Texture", &config.texturePath);
					if (ImGui::Button("Reload Texture")) {
						emitter.LoadTexture();
					}
				}
				ImGui::Checkbox("Additive", &config.additiveBlending);
				ImGui::TreePop();
			}

			// Presets
			if (ImGui::TreeNode("Apply Preset")) {
				if (ImGui::Button("Smoke")) {
					config.ApplyPreset("Smoke");
				}
				ImGui::SameLine();
				if (ImGui::Button("Fire")) {
					config.ApplyPreset("Fire");
				}
				ImGui::SameLine();
				if (ImGui::Button("Sparks")) {
					config.ApplyPreset("Sparks");
				}
				if (ImGui::Button("Snow")) {
					config.ApplyPreset("Snow");
				}
				ImGui::SameLine();
				if (ImGui::Button("Explosion")) {
					config.ApplyPreset("Explosion");
				}
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
