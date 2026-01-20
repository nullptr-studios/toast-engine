/// @file ParticleSystem.hpp
/// @date 01/20/2026
/// @brief GPU-based Particle System using compute shaders

#pragma once

#include "Toast/Objects/Actor.hpp"
#include "Toast/Renderer/IRenderable.hpp"
#include "Toast/Renderer/Shader.hpp"
#include "Toast/Resources/Texture.hpp"

#include <glad/gl.h>
#include <glm/glm.hpp>

namespace toast {

/// @brief Emission mode for particle emitters
enum class EmissionMode : uint8_t {
	Continuous,  ///< Emit particles continuously at a rate
	Burst        ///< Emit particles in bursts
};

/// @brief Shape of the emitter volume
enum class EmitterShape : uint8_t {
	Point,       ///< Emit from a single point
	Sphere,      ///< Emit from a sphere volume
	Box,         ///< Emit from a box volume
	Cone         ///< Emit in a cone direction
};

/// @brief A single burst event configuration
struct ParticleBurst {
	float time = 0.0f;           ///< Time offset from system start
	uint32_t count = 10;         ///< Number of particles to emit
	float cycleInterval = 0.0f;  ///< If > 0, burst repeats at this interval
	bool triggered = false;      ///< Internal: has this burst been triggered this cycle
};

/// @brief Range helper for random values
template<typename T>
struct Range {
	T min {};
	T max {};
	
	Range() = default;
	Range(T val) : min(val), max(val) {}
	Range(T minVal, T maxVal) : min(minVal), max(maxVal) {}
};

/// @brief Configuration for a particle emitter
struct ParticleEmitterConfig {
	// Emission
	EmissionMode emissionMode = EmissionMode::Continuous;
	float emissionRate = 10.0f;               ///< Particles per second (continuous mode)
	std::vector<ParticleBurst> bursts;        ///< Burst configurations
	
	// Shape
	EmitterShape shape = EmitterShape::Point;
	glm::vec3 shapeSize = glm::vec3(1.0f);    ///< Size of emission shape (sphere radius, box dimensions, etc.)
	float coneAngle = 45.0f;                  ///< Cone half-angle in degrees
	
	// Lifetime
	Range<float> lifetime = { 1.0f, 2.0f };
	
	// Initial velocity
	Range<float> speed = { 1.0f, 3.0f };
	glm::vec3 direction = glm::vec3(0.0f, 1.0f, 0.0f);
	float directionRandomness = 0.0f;         ///< 0 = exact direction, 1 = fully random
	
	// Size
	Range<float> startSize = { 0.5f, 1.0f };
	Range<float> endSize = { 0.1f, 0.2f };
	
	// Rotation
	Range<float> startRotation = { 0.0f, 360.0f };
	Range<float> rotationSpeed = { 0.0f, 0.0f };  ///< Degrees per second
	
	// Color
	glm::vec4 startColor = glm::vec4(1.0f);
	glm::vec4 endColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
	bool randomizeStartColor = false;
	glm::vec4 startColorRangeMin = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
	glm::vec4 startColorRangeMax = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	
	// Physics
	glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f);
	float drag = 0.0f;                        ///< Velocity damping factor
	
	// Texture
	std::string texturePath = "";
	bool useTexture = false;
	bool additiveBlending = false;
};

/// @brief GPU particle data structure
struct alignas(16) GPUParticle {
	glm::vec4 pos;      ///< xyz = position, w = currentSize
	glm::vec4 vel;      ///< xyz = velocity, w = rotation
	glm::vec4 color;    ///< current color RGBA (interpolated)
	glm::vec4 end;      ///< end color RGBA
	glm::vec4 misc;     ///< x = lifeRemaining, y = lifeMax, z = seed, w = endSize
	glm::vec4 extra;    ///< x = startSize, y = rotationSpeed, z = drag, w = unused
};

class ParticleSystem : public renderer::IRenderable {
public:
	REGISTER_TYPE(ParticleSystem);

	ParticleSystem();
	~ParticleSystem() override;

	void Init() override;
	void Destroy() override;
	void Tick() override;
	
	void Load(json_t j, bool force_create = true) override;
	[[nodiscard]]
	json_t Save() const override;

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

	// Particle System Control
	
	/// @brief Start/resume the particle system
	void Play();
	
	/// @brief Pause emission
	void Pause();
	
	/// @brief Stop and clear all particles
	void Stop();
	
	/// @brief Emit a burst of particles immediately
	/// @param count Number of particles to emit
	void EmitBurst(uint32_t count);
	
	/// @brief Check if the system is playing
	[[nodiscard]]
	bool IsPlaying() const { return m_isPlaying; }
	
	/// @brief Get current particle count
	[[nodiscard]]
	uint32_t GetParticleCount() const { return m_aliveCount; }
	
	// Config
	
	/// @brief Get the emitter configuration
	[[nodiscard]]
	ParticleEmitterConfig& GetConfig() { return m_config; }
	
	/// @brief Get the emitter configuration (const)
	[[nodiscard]]
	const ParticleEmitterConfig& GetConfig() const { return m_config; }
	
	/// @brief Set maximum particles
	void SetMaxParticles(uint32_t max);
	
	/// @brief Get maximum particles
	[[nodiscard]]
	uint32_t GetMaxParticles() const { return m_maxParticles; }

private:
	
	/// @brief Initialize GPU resources
	void InitGPUResources();
	
	/// @brief Cleanup GPU resources
	void CleanupGPUResources();
	
	/// @brief Update particles using compute shader and render
	void UpdateAndRender(const glm::mat4& viewProjection);
	
	/// @brief Spawn new particles on CPU and upload to GPU
	void SpawnParticles(uint32_t count);
	
	/// @brief Generate a random position based on emitter shape
	[[nodiscard]]
	glm::vec3 GenerateSpawnPosition();
	
	/// @brief Generate a random velocity based on emitter settings
	[[nodiscard]]
	glm::vec3 GenerateSpawnVelocity();
	
	/// @brief Random float in range [min, max]
	[[nodiscard]]
	float RandomFloat(float min, float max);
	
	/// @brief Random vec3 with each component in [-1, 1]
	[[nodiscard]]
	glm::vec3 RandomDirection();
	
	
	

	ParticleEmitterConfig m_config;
	uint32_t m_maxParticles = 10000;
	

	bool m_isPlaying = true;
	bool m_gpuInitialized = false;
	float m_systemTime = 0.0f;
	float m_emissionAccumulator = 0.0f;
	uint32_t m_aliveCount = 0;
	
	
	GLuint m_particleBuffers[2] = { 0, 0 };  ///< Double-buffered particle SSBOs
	GLuint m_counterBuffer = 0;               ///< Atomic counters SSBO
	GLuint m_frameParamsUBO = 0;              ///< Frame parameters UBO
	GLuint m_quadVAO = 0;                     ///< Quad VAO for rendering
	GLuint m_quadVBO = 0;                     ///< Quad VBO for rendering
	int m_currentBuffer = 0;                  ///< Current read buffer index
	

	std::shared_ptr<renderer::Shader> m_computeShader;
	std::shared_ptr<renderer::Shader> m_renderShader;
	

	std::shared_ptr<Texture> m_texture;
	

	std::mt19937 m_rng;
	std::uniform_real_distribution<float> m_dist { 0.0f, 1.0f };
	
	
	int m_cullingRadius = 20; ///< Radius for frustum culling
	
	// IRenderable implementation
	void OnRender(const glm::mat4& viewProjection) noexcept override;
	
};

}    // namespace toast
