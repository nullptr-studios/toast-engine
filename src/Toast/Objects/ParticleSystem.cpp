/// @file ParticleSystem.cpp
/// @date 01/20/2026
/// @brief GPU-based Particle System implementation

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

#ifdef TOAST_EDITOR
#include "imgui.h"
#include "imgui_stdlib.h"
#endif

namespace toast {


// Renderable
void ParticleSystem::OnRender(const glm::mat4& viewProjection) noexcept {
	if (m_gpuInitialized) {
		UpdateAndRender(viewProjection);
	}
}


ParticleSystem::ParticleSystem() 
	: m_rng(std::random_device{}()) {
}

ParticleSystem::~ParticleSystem() {
	CleanupGPUResources();
}

void ParticleSystem::Init() {
	TransformComponent::Init();
	
	// Setup default smoke-like particle system
	m_config.emissionMode = EmissionMode::Continuous;
	m_config.emissionRate = 20.0f;
	
	m_config.shape = EmitterShape::Sphere;
	m_config.shapeSize = glm::vec3(0.5f);
	
	m_config.lifetime = { 2.0f, 4.0f };
	
	m_config.speed = { 0.5f, 1.5f };
	m_config.direction = glm::vec3(0.0f, 1.0f, 0.0f);
	m_config.directionRandomness = 0.3f;
	
	m_config.startSize = { 0.3f, 0.5f };
	m_config.endSize = { 1.0f, 2.0f };
	
	m_config.startRotation = { 0.0f, 360.0f };
	m_config.rotationSpeed = { -30.0f, 30.0f };
	
	// Smoke colors: gray with fade out
	m_config.startColor = glm::vec4(0.5f, 0.5f, 0.5f, 0.6f);
	m_config.endColor = glm::vec4(0.3f, 0.3f, 0.3f, 0.0f);
	
	m_config.gravity = glm::vec3(0.0f, 0.2f, 0.0f);  // Slight upward drift
	m_config.drag = 0.5f;
	
	m_config.texturePath = "placeholder";
	m_config.useTexture = true;
	m_config.additiveBlending = false;
	
	// Initialize GPU resources
	InitGPUResources();
	
	// Create and register the renderable
	renderer::IRendererBase::GetInstance()->AddRenderable(this);
	
	TOAST_INFO("ParticleSystem initialized");
}

void ParticleSystem::Destroy() {
	renderer::IRendererBase::GetInstance()->RemoveRenderable(this);
	
	CleanupGPUResources();
	TransformComponent::Destroy();
}

void ParticleSystem::Tick() {
	TransformComponent::Tick();
	// NOTE: Actual particle update happens in OnRender
}

void ParticleSystem::Load(json_t j, bool force_create) {
	TransformComponent::Load(j, force_create);
	//TODO
}

json_t ParticleSystem::Save() const {
	json_t j = TransformComponent::Save();
	
	// TODO: Save emitter config
	
	return j;
}

void ParticleSystem::Play() {
	m_isPlaying = true;
}

void ParticleSystem::Pause() {
	m_isPlaying = false;
}

void ParticleSystem::Stop() {
	m_isPlaying = false;
	m_aliveCount = 0;
	m_systemTime = 0.0f;
	m_emissionAccumulator = 0.0f;
	
	// Reset burst triggers
	for (auto& burst : m_config.bursts) {
		burst.triggered = false;
	}
	
	// Clear GPU buffer by resetting counter
	if (m_counterBuffer) {
		uint32_t zeros[4] = { 0, 0, 0, 0 };
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_counterBuffer);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(zeros), zeros);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}
}

void ParticleSystem::EmitBurst(uint32_t count) {
	SpawnParticles(count);
}

void ParticleSystem::SetMaxParticles(uint32_t max) {
	if (max == m_maxParticles) return;
	
	m_maxParticles = max;
	
	// Reinitialize GPU resources with new size
	if (m_gpuInitialized) {
		CleanupGPUResources();
		InitGPUResources();
	}
}

void ParticleSystem::InitGPUResources() {
	if (m_gpuInitialized) return;
	
	TOAST_INFO("Initializing ParticleSystem GPU resources (max: {})", m_maxParticles);
	
	// Load shaders
	m_computeShader = resource::ResourceManager::GetInstance()->LoadResource<renderer::Shader>("shaders/particles_compute.shader");
	m_renderShader = resource::ResourceManager::GetInstance()->LoadResource<renderer::Shader>("shaders/particles_render.shader");
	
	// Load texture
	if (m_config.useTexture && !m_config.texturePath.empty()) {
		m_texture = resource::ResourceManager::GetInstance()->LoadResource<Texture>(m_config.texturePath);
	}
	
	// Create double-buffered particle SSBOs
	const size_t bufferSize = sizeof(GPUParticle) * m_maxParticles;
	
	glGenBuffers(2, m_particleBuffers);
	for (int i = 0; i < 2; ++i) {
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_particleBuffers[i]);
		glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, nullptr, GL_DYNAMIC_DRAW);
	}
	
	// Create counter buffer: [inCount, outCount, spawnCount, pad]
	glGenBuffers(1, &m_counterBuffer);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_counterBuffer);
	uint32_t initialCounters[4] = { 0, 0, 0, 0 };
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(initialCounters), initialCounters, GL_DYNAMIC_DRAW);
	
	// Create frame parameters UBO
	glGenBuffers(1, &m_frameParamsUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, m_frameParamsUBO);
	// Layout: float dt, vec3 gravity, uint maxParticles, float drag
	float frameParams[8] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
	glBufferData(GL_UNIFORM_BUFFER, sizeof(frameParams), frameParams, GL_DYNAMIC_DRAW);
	
	// Create quad VAO/VBO for rendering
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
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	
	m_gpuInitialized = true;
	m_currentBuffer = 0;
}

void ParticleSystem::CleanupGPUResources() {
	if (!m_gpuInitialized) return;
	
	if (m_quadVAO) {
		glDeleteVertexArrays(1, &m_quadVAO);
		m_quadVAO = 0;
	}
	if (m_quadVBO) {
		glDeleteBuffers(1, &m_quadVBO);
		m_quadVBO = 0;
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
	
	m_computeShader.reset();
	m_renderShader.reset();
	m_texture.reset();
	
	m_gpuInitialized = false;
}

void ParticleSystem::UpdateAndRender(const glm::mat4& viewProjection) {
	if (!m_gpuInitialized || !m_computeShader || !m_renderShader) {
		return;
	}
	
	if (OclussionVolume::isSphereOnPlanes(renderer::IRendererBase::GetInstance()->GetFrustumPlanes(), worldPosition(), m_cullingRadius)) {
		// Not visible, skip update and render
		return;
	}
	
	PROFILE_ZONE;
	
	const float dt = static_cast<float>(Time::delta());
	
	// Update system time
	if (m_isPlaying) {
		m_systemTime += dt;
		
		// Handle continuous emission
		if (m_config.emissionMode == EmissionMode::Continuous) {
			m_emissionAccumulator += m_config.emissionRate * dt;
			const uint32_t toSpawn = static_cast<uint32_t>(m_emissionAccumulator);
			if (toSpawn > 0) {
				SpawnParticles(toSpawn);
				m_emissionAccumulator -= static_cast<float>(toSpawn);
			}
		}
		
		// Handle burst emission
		for (auto& burst : m_config.bursts) {
			if (!burst.triggered && m_systemTime >= burst.time) {
				SpawnParticles(burst.count);
				burst.triggered = true;
			}
			// Handle repeating bursts
			if (burst.cycleInterval > 0.0f && burst.triggered) {
				const float cycleTime = fmod(m_systemTime - burst.time, burst.cycleInterval);
				if (cycleTime < dt) {
					SpawnParticles(burst.count);
				}
			}
		}
	}
	
	// Skip if no particles
	if (m_aliveCount == 0) {
		return;
	}
	
	// COMPUTE PASS
	
	// Update frame parameters UBO
	struct FrameParams {
		float dt;
		float gravityX, gravityY, gravityZ;
		uint32_t maxParticles;
		float drag;
		float pad1, pad2;
	} params;
	
	params.dt = dt;
	params.gravityX = m_config.gravity.x;
	params.gravityY = m_config.gravity.y;
	params.gravityZ = m_config.gravity.z;
	params.maxParticles = m_maxParticles;
	params.drag = m_config.drag;
	params.pad1 = params.pad2 = 0.0f;
	
	glBindBuffer(GL_UNIFORM_BUFFER, m_frameParamsUBO);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(params), &params);
	
	// Reset output counter
	uint32_t zero = 0;
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_counterBuffer);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(uint32_t), sizeof(uint32_t), &zero);  // outCount = 0
	
	// Set input count
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t), &m_aliveCount);  // inCount = current alive
	
	// Bind buffers for compute
	const int readBuffer = m_currentBuffer;
	const int writeBuffer = 1 - m_currentBuffer;
	
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_particleBuffers[readBuffer]);   // Input
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_particleBuffers[writeBuffer]);  // Output
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_counterBuffer);
	glBindBufferBase(GL_UNIFORM_BUFFER, 4, m_frameParamsUBO);
	
	// Dispatch compute shader
	m_computeShader->Use();
	
	const uint32_t workGroups = (m_aliveCount + 255) / 256;
	glDispatchCompute(workGroups, 1, 1);
	
	// Memory barrier before reading results
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	
	// Read back new alive count
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_counterBuffer);
	glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(uint32_t), sizeof(uint32_t), &m_aliveCount);
	
	// Swap buffers
	m_currentBuffer = writeBuffer;
	
	// RENDER PASS
	
	if (m_aliveCount == 0) {
		return;
	}
	
	// billboarding
	glm::mat4 viewMatrix = renderer::IRendererBase::GetInstance()->GetViewMatrix();
	glm::vec3 camRight = glm::vec3(viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0]);
	glm::vec3 camUp = glm::vec3(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);
	
	// Setup blending
	glEnable(GL_BLEND);
	if (m_config.additiveBlending) {
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	} else {
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	
	// Disable depth writing for particles
	glDepthMask(GL_FALSE);
	
	// Bind render shader
	m_renderShader->Use();
	m_renderShader->Set("u_ViewProj", viewProjection);
	m_renderShader->Set("u_CamRight", camRight);
	m_renderShader->Set("u_CamUp", camUp);
	
	// Bind texture
	if (m_texture && m_config.useTexture) {
		m_texture->Bind(1);
		m_renderShader->SetSampler("u_Tex", 1);
	}
	
	// Bind particle buffer
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_particleBuffers[m_currentBuffer]);
	
	// Draw instanced quads
	glBindVertexArray(m_quadVAO);
	glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(m_aliveCount));
	glBindVertexArray(0);
	
	// Restore state
	glDepthMask(GL_TRUE);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	
	if (m_texture) {
		m_texture->Unbind(1);
	}
}

void ParticleSystem::SpawnParticles(uint32_t count) {
	if (!m_gpuInitialized || count == 0) return;
	
	// Clamp to available space
	const uint32_t available = m_maxParticles - m_aliveCount;
	count = std::min(count, available);
	
	if (count == 0) 
		return;
	
	// Generate particles on CPU
	std::vector<GPUParticle> newParticles(count);
	
	const glm::vec3 worldPos = worldPosition();
	
	for (uint32_t i = 0; i < count; ++i) {
		GPUParticle& p = newParticles[i];
		
		// Position
		glm::vec3 spawnOffset = GenerateSpawnPosition();
		glm::vec3 pos = worldPos + spawnOffset;
		
		// Size
		float startSize = RandomFloat(m_config.startSize.min, m_config.startSize.max);
		float endSize = RandomFloat(m_config.endSize.min, m_config.endSize.max);
		
		p.pos = glm::vec4(pos, startSize);
		
		// Velocity
		glm::vec3 vel = GenerateSpawnVelocity();
		float rotation = glm::radians(RandomFloat(m_config.startRotation.min, m_config.startRotation.max));
		p.vel = glm::vec4(vel, rotation);
		
		// Color
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
		
		// Lifetime and other
		float lifetime = RandomFloat(m_config.lifetime.min, m_config.lifetime.max);
		float seed = m_dist(m_rng);
		p.misc = glm::vec4(lifetime, lifetime, seed, endSize);
		
		// Extra data
		float rotSpeed = glm::radians(RandomFloat(m_config.rotationSpeed.min, m_config.rotationSpeed.max));
		p.extra = glm::vec4(startSize, rotSpeed, m_config.drag, 0.0f);
	}
	
	// Upload to GPU
	const int writeBuffer = m_currentBuffer;
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_particleBuffers[writeBuffer]);
	
	// Append after existing particles
	const size_t offset = sizeof(GPUParticle) * m_aliveCount;
	const size_t size = sizeof(GPUParticle) * count;
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), newParticles.data());
	
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	
	m_aliveCount += count;
}

glm::vec3 ParticleSystem::GenerateSpawnPosition() {
	switch (m_config.shape) {

			
		case EmitterShape::Sphere: {
			// Random point in sphere (THIS IS NOT THE BEST WAY TO DO IT PLEASE OPTIMIZE)
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

glm::vec3 ParticleSystem::GenerateSpawnVelocity() {
	const float speed = RandomFloat(m_config.speed.min, m_config.speed.max);
	
	glm::vec3 dir = glm::normalize(m_config.direction);
	
	if (m_config.shape == EmitterShape::Cone) {
		// Cone emission
		const float halfAngle = glm::radians(m_config.coneAngle);
		const float cosAngle = std::cos(halfAngle);
		
		// Random direction within cone
		const float z = RandomFloat(cosAngle, 1.0f);
		const float phi = RandomFloat(0.0f, 2.0f * glm::pi<float>());
		const float sinTheta = std::sqrt(1.0f - z * z);
		
		glm::vec3 localDir(sinTheta * std::cos(phi), sinTheta * std::sin(phi), z);
		
		// Rotate to align with emission direction
		glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f);
		if (std::abs(glm::dot(dir, up)) > 0.999f) {
			up = glm::vec3(1.0f, 0.0f, 0.0f);
		}
		glm::vec3 right = glm::normalize(glm::cross(up, dir));
		up = glm::cross(dir, right);
		
		dir = localDir.x * right + localDir.y * up + localDir.z * dir;
	} else if (m_config.directionRandomness > 0.0f) {
		// Add randomness to direction
		glm::vec3 randomDir = RandomDirection();
		dir = glm::normalize(glm::mix(dir, randomDir, m_config.directionRandomness));
	}
	
	return dir * speed;
}

float ParticleSystem::RandomFloat(float min, float max) {
	return min + m_dist(m_rng) * (max - min);
}

glm::vec3 ParticleSystem::RandomDirection() {
	// Generate uniformly distributed direction on unit sphere
	const float theta = RandomFloat(0.0f, 2.0f * glm::pi<float>());
	const float phi = std::acos(RandomFloat(-1.0f, 1.0f));
	
	return glm::vec3(
		std::sin(phi) * std::cos(theta),
		std::sin(phi) * std::sin(theta),
		std::cos(phi)
	);
}

#ifdef TOAST_EDITOR
void ParticleSystem::Inspector() {
	TransformComponent::Inspector();
	
	ImGui::DragInt("Culling Radius", &m_cullingRadius, 1, 1, 1000);
	
	ImGui::Separator();
	ImGui::Text("Particle System");
	ImGui::Separator();
	
	
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
	
	ImGui::Text("Particles: %u / %u", m_aliveCount, m_maxParticles);
	ImGui::Text("System Time: %.2f s", m_systemTime);
	
	int maxP = static_cast<int>(m_maxParticles);
	if (ImGui::DragInt("Max Particles", &maxP, 100, 100, 100000)) {
		SetMaxParticles(static_cast<uint32_t>(maxP));
	}
	
	ImGui::Separator();
	
	// Emission
	if (ImGui::CollapsingHeader("Emission", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Indent(10);
		
		const char* emissionModes[] = { "Continuous", "Burst" };
		int currentMode = static_cast<int>(m_config.emissionMode);
		if (ImGui::Combo("Emission Mode", &currentMode, emissionModes, 2)) {
			m_config.emissionMode = static_cast<EmissionMode>(currentMode);
		}
		
		if (m_config.emissionMode == EmissionMode::Continuous) {
			ImGui::DragFloat("Emission Rate", &m_config.emissionRate, 0.5f, 0.0f, 1000.0f, "%.1f/s");
		}
		
		// Burst configuration
		ImGui::Text("Bursts:");
		for (size_t i = 0; i < m_config.bursts.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			auto& burst = m_config.bursts[i];
			
			ImGui::DragFloat("Time", &burst.time, 0.1f, 0.0f, 100.0f);
			ImGui::SameLine();
			int bCount = static_cast<int>(burst.count);
			if (ImGui::DragInt("Count", &bCount, 1, 1, 1000)) {
				burst.count = static_cast<uint32_t>(bCount);
			}
			ImGui::SameLine();
			ImGui::DragFloat("Repeat", &burst.cycleInterval, 0.1f, 0.0f, 10.0f);
			ImGui::SameLine();
			if (ImGui::Button("X")) {
				m_config.bursts.erase(m_config.bursts.begin() + i);
				ImGui::PopID();
				break;
			}
			
			ImGui::PopID();
		}
		if (ImGui::Button("Add Burst")) {
			m_config.bursts.push_back({});
		}
		
		ImGui::Unindent(10);
	}
	
	// Shape
	if (ImGui::CollapsingHeader("Shape", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Indent(10);
		
		const char* shapes[] = { "Point", "Sphere", "Box", "Cone" };
		int currentShape = static_cast<int>(m_config.shape);
		if (ImGui::Combo("Shape", &currentShape, shapes, 4)) {
			m_config.shape = static_cast<EmitterShape>(currentShape);
		}
		
		switch (m_config.shape) {
			case EmitterShape::Sphere:
				ImGui::DragFloat("Radius", &m_config.shapeSize.x, 0.1f, 0.0f, 100.0f);
				break;
			case EmitterShape::Box:
				ImGui::DragFloat3("Size", &m_config.shapeSize.x, 0.1f, 0.0f, 100.0f);
				break;
			case EmitterShape::Cone:
				ImGui::DragFloat("Angle", &m_config.coneAngle, 1.0f, 0.0f, 90.0f);
				break;
			default:
				break;
		}
		
		ImGui::Unindent(10);
	}
	
	// Lifetime
	if (ImGui::CollapsingHeader("Lifetime", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Indent(10);
		
		ImGui::DragFloatRange2("Lifetime", &m_config.lifetime.min, &m_config.lifetime.max, 0.1f, 0.01f, 60.0f, "Min: %.2f", "Max: %.2f");
		
		ImGui::Unindent(10);
	}
	
	// Velocity
	if (ImGui::CollapsingHeader("Velocity", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Indent(10);
		
		ImGui::DragFloatRange2("Speed", &m_config.speed.min, &m_config.speed.max, 0.1f, 0.0f, 100.0f, "Min: %.2f", "Max: %.2f");
		ImGui::DragFloat3("Direction", &m_config.direction.x, 0.1f, -1.0f, 1.0f);
		if (ImGui::Button("Normalize Dir")) {
			if (glm::length(m_config.direction) > 0.001f) {
				m_config.direction = glm::normalize(m_config.direction);
			}
		}
		ImGui::DragFloat("Direction Randomness", &m_config.directionRandomness, 0.01f, 0.0f, 1.0f);
		
		ImGui::Unindent(10);
	}
	
	// Size
	if (ImGui::CollapsingHeader("Size", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Indent(10);
		
		ImGui::DragFloatRange2("Start Size", &m_config.startSize.min, &m_config.startSize.max, 0.05f, 0.01f, 50.0f, "Min: %.2f", "Max: %.2f");
		ImGui::DragFloatRange2("End Size", &m_config.endSize.min, &m_config.endSize.max, 0.05f, 0.01f, 50.0f, "Min: %.2f", "Max: %.2f");
		
		ImGui::Unindent(10);
	}
	
	// Rotation
	if (ImGui::CollapsingHeader("Rotation")) {
		ImGui::Indent(10);
		
		ImGui::DragFloatRange2("Start Rotation", &m_config.startRotation.min, &m_config.startRotation.max, 1.0f, 0.0f, 360.0f, "Min: %.0f", "Max: %.0f");
		ImGui::DragFloatRange2("Rotation Speed", &m_config.rotationSpeed.min, &m_config.rotationSpeed.max, 1.0f, -360.0f, 360.0f, "Min: %.0f/s", "Max: %.0f/s");
		
		ImGui::Unindent(10);
	}
	
	// Color
	if (ImGui::CollapsingHeader("Color", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Indent(10);
		
		ImGui::Checkbox("Randomize Start Color", &m_config.randomizeStartColor);
		
		if (m_config.randomizeStartColor) {
			ImGui::ColorEdit4("Start Color Min", &m_config.startColorRangeMin.r);
			ImGui::ColorEdit4("Start Color Max", &m_config.startColorRangeMax.r);
		} else {
			ImGui::ColorEdit4("Start Color", &m_config.startColor.r);
		}
		ImGui::ColorEdit4("End Color", &m_config.endColor.r);
		
		ImGui::Unindent(10);
	}
	
	// Physics
	if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Indent(10);
		
		ImGui::DragFloat3("Gravity", &m_config.gravity.x, 0.1f, -100.0f, 100.0f);
		ImGui::DragFloat("Drag", &m_config.drag, 0.01f, 0.0f, 10.0f);
		
		ImGui::Unindent(10);
	}
	
	// Rendering
	if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Indent(10);
		
		ImGui::Checkbox("Use Texture", &m_config.useTexture);
		if (m_config.useTexture) {
			char texPath[256];
			strncpy(texPath, m_config.texturePath.c_str(), sizeof(texPath) - 1);
			texPath[sizeof(texPath) - 1] = '\0';
			if (ImGui::InputText("Texture Path", texPath, sizeof(texPath))) {
				m_config.texturePath = texPath;
			}
			if (ImGui::Button("Reload Texture")) {
				m_texture = resource::ResourceManager::GetInstance()->LoadResource<Texture>(m_config.texturePath);
			}
		}
		
		ImGui::Checkbox("Additive Blending", &m_config.additiveBlending);
		
		ImGui::Unindent(10);
	}
	

	if (ImGui::CollapsingHeader("Presets")) {
		ImGui::Indent(10);
		
		if (ImGui::Button("Smoke")) {
			m_config.emissionMode = EmissionMode::Continuous;
			m_config.emissionRate = 20.0f;
			m_config.shape = EmitterShape::Sphere;
			m_config.shapeSize = glm::vec3(0.5f);
			m_config.lifetime = { 2.0f, 4.0f };
			m_config.speed = { 0.5f, 1.5f };
			m_config.direction = glm::vec3(0.0f, 1.0f, 0.0f);
			m_config.directionRandomness = 0.3f;
			m_config.startSize = { 0.3f, 0.5f };
			m_config.endSize = { 1.0f, 2.0f };
			m_config.startColor = glm::vec4(0.5f, 0.5f, 0.5f, 0.6f);
			m_config.endColor = glm::vec4(0.3f, 0.3f, 0.3f, 0.0f);
			m_config.gravity = glm::vec3(0.0f, 0.2f, 0.0f);
			m_config.drag = 0.5f;
			m_config.additiveBlending = false;
		}
		ImGui::SameLine();
		if (ImGui::Button("Fire")) {
			m_config.emissionMode = EmissionMode::Continuous;
			m_config.emissionRate = 50.0f;
			m_config.shape = EmitterShape::Cone;
			m_config.coneAngle = 15.0f;
			m_config.lifetime = { 0.5f, 1.5f };
			m_config.speed = { 2.0f, 4.0f };
			m_config.direction = glm::vec3(0.0f, 1.0f, 0.0f);
			m_config.startSize = { 0.2f, 0.4f };
			m_config.endSize = { 0.05f, 0.1f };
			m_config.startColor = glm::vec4(1.0f, 0.8f, 0.2f, 1.0f);
			m_config.endColor = glm::vec4(1.0f, 0.2f, 0.0f, 0.0f);
			m_config.gravity = glm::vec3(0.0f, 1.0f, 0.0f);
			m_config.drag = 0.2f;
			m_config.additiveBlending = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Sparks")) {
			m_config.emissionMode = EmissionMode::Burst;
			m_config.bursts = { { 0.0f, 50, 0.5f, false } };
			m_config.shape = EmitterShape::Point;
			m_config.lifetime = { 0.3f, 0.8f };
			m_config.speed = { 5.0f, 10.0f };
			m_config.direction = glm::vec3(0.0f, 1.0f, 0.0f);
			m_config.directionRandomness = 1.0f;
			m_config.startSize = { 0.05f, 0.1f };
			m_config.endSize = { 0.01f, 0.02f };
			m_config.startColor = glm::vec4(1.0f, 0.9f, 0.5f, 1.0f);
			m_config.endColor = glm::vec4(1.0f, 0.5f, 0.0f, 0.0f);
			m_config.gravity = glm::vec3(0.0f, -15.0f, 0.0f);
			m_config.drag = 0.0f;
			m_config.additiveBlending = true;
		}
		
		if (ImGui::Button("Snow")) {
			m_config.emissionMode = EmissionMode::Continuous;
			m_config.emissionRate = 30.0f;
			m_config.shape = EmitterShape::Box;
			m_config.shapeSize = glm::vec3(10.0f, 0.1f, 10.0f);
			m_config.lifetime = { 3.0f, 5.0f };
			m_config.speed = { 0.2f, 0.5f };
			m_config.direction = glm::vec3(0.0f, -1.0f, 0.0f);
			m_config.directionRandomness = 0.1f;
			m_config.startSize = { 0.05f, 0.15f };
			m_config.endSize = { 0.05f, 0.15f };
			m_config.startColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.8f);
			m_config.endColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
			m_config.gravity = glm::vec3(0.0f, -0.5f, 0.0f);
			m_config.drag = 0.3f;
			m_config.additiveBlending = false;
		}
		ImGui::SameLine();
		if (ImGui::Button("Explosion")) {
			m_config.emissionMode = EmissionMode::Burst;
			m_config.bursts = { { 0.0f, 100, 0.0f, false } };
			m_config.shape = EmitterShape::Point;
			m_config.lifetime = { 0.5f, 1.5f };
			m_config.speed = { 3.0f, 8.0f };
			m_config.direction = glm::vec3(0.0f, 1.0f, 0.0f);
			m_config.directionRandomness = 1.0f;
			m_config.startSize = { 0.3f, 0.6f };
			m_config.endSize = { 0.1f, 0.2f };
			m_config.startColor = glm::vec4(1.0f, 0.6f, 0.1f, 1.0f);
			m_config.endColor = glm::vec4(0.3f, 0.1f, 0.0f, 0.0f);
			m_config.gravity = glm::vec3(0.0f, -5.0f, 0.0f);
			m_config.drag = 1.0f;
			m_config.additiveBlending = true;
		}
		
		ImGui::Unindent(10);
	}
}
#endif

}
