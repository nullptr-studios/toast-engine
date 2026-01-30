/// @file ParticleSystem.hpp
/// @date 01/20/2026
/// @brief GPU-based Particle System using compute shaders

#pragma once

#include "Toast/Components/TransformComponent.hpp"
#include "Toast/Objects/Actor.hpp"
#include "Toast/Renderer/IRenderable.hpp"
#include "Toast/Renderer/Shader.hpp"
#include "Toast/Resources/Texture.hpp"

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <sol/sol.hpp>

namespace toast {

/// @brief Emission mode for particle emitters
enum class EmissionMode : uint8_t {
	Continuous,    ///< Emit particles continuously at a rate
	Burst          ///< Emit particles in bursts
};

/// @brief Shape of the emitter volume
enum class EmitterShape : uint8_t {
	Point,     ///< Emit from a single point
	Sphere,    ///< Emit from a sphere volume
	Box,       ///< Emit from a box volume
	Cone       ///< Emit in a cone direction
};

/// @brief A single burst event configuration
struct ParticleBurst {
	float time = 0.0f;             ///< Time offset from system start
	uint32_t count = 10;           ///< Number of particles to emit
	float cycleInterval = 0.0f;    ///< If > 0, burst repeats at this interval
	bool triggered = false;        ///< Internal: has this burst been triggered this cycle

	/// @brief Load burst from Lua table
	void LoadFromLua(const sol::table& table);
};

/// @brief Range helper for random values
template<typename T>
struct Range {
	T min {};
	T max {};

	Range() = default;

	Range(T val) : min(val), max(val) { }

	Range(T minVal, T maxVal) : min(minVal), max(maxVal) { }

	/// @brief Load range from Lua table {min, max}
	void LoadFromLua(const sol::table& table) {
		if (table.size() >= 2) {
			sol::optional<T> minOpt = table[1];
			sol::optional<T> maxOpt = table[2];
			if (minOpt) {
				min = *minOpt;
			}
			if (maxOpt) {
				max = *maxOpt;
			}
		} else if (table.size() == 1) {
			sol::optional<T> valOpt = table[1];
			if (valOpt) {
				min = max = *valOpt;
			}
		}
	}

	/// @brief Load range from Lua
	void LoadFromLua(const sol::object& obj) {
		if (obj.is<sol::table>()) {
			LoadFromLua(obj.as<sol::table>());
		} else if (obj.is<T>()) {
			min = max = obj.as<T>();
		}
	}
};

/// @brief Configuration for a particle emitter
struct ParticleEmitterConfig {
	// Emitter identification
	std::string name = "Emitter";
	bool enabled = true;

	// Emission
	EmissionMode emissionMode = EmissionMode::Continuous;
	float emissionRate = 10.0f;           ///< Particles per second (continuous mode)
	std::vector<ParticleBurst> bursts;    ///< Burst configurations
	bool looping = true;                  ///< If false, emitter plays once then stops
	float duration = 5.0f;                ///< Duration of one emission cycle

	// Shape
	EmitterShape shape = EmitterShape::Point;
	glm::vec3 shapeSize = glm::vec3(1.0f);    ///< Size of emission shape
	float coneAngle = 45.0f;                  ///< Cone half-angle in degrees

	// Offset from particle system position
	glm::vec3 localOffset = glm::vec3(0.0f);
	glm::vec3 localRotation = glm::vec3(0.0f);    ///< Local rotation

	// Lifetime
	Range<float> lifetime = { 1.0f, 2.0f };

	// Initial velocity
	Range<float> speed = { 1.0f, 3.0f };
	glm::vec3 direction = glm::vec3(0.0f, 1.0f, 0.0f);
	float directionRandomness = 0.0f;    ///< 0 = exact direction, 1 = fully random

	// Size
	Range<float> startSize = { 0.5f, 1.0f };
	Range<float> endSize = { 0.1f, 0.2f };

	// Rotation
	Range<float> startRotation = { 0.0f, 360.0f };
	Range<float> rotationSpeed = { 0.0f, 0.0f };    ///< Degrees per second

	// Color
	glm::vec4 startColor = glm::vec4(1.0f);
	glm::vec4 endColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
	bool randomizeStartColor = false;
	glm::vec4 startColorRangeMin = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
	glm::vec4 startColorRangeMax = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

	// Physics
	glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f);
	float drag = 0.0f;    ///< Velocity damping factor

	// Texture
	std::string texturePath = "";
	bool useTexture = false;
	bool additiveBlending = false;

	// Max particles for this emitter
	static constexpr uint32_t MAX_PARTICLES_LIMIT = 1000000;    // 1 million particles (96MB per buffer)
	uint32_t maxParticles = 10000;

	/// @brief Load configuration from Lua table
	void LoadFromLua(const sol::table& table);

	/// @brief Apply a preset configuration
	void ApplyPreset(const std::string& presetName);
};

/// @brief GPU particle data structure
struct alignas(16) GPUParticle {
	glm::vec4 pos;      ///< xyz = position, w = currentSize
	glm::vec4 vel;      ///< xyz = velocity, w = rotation
	glm::vec4 color;    ///< current color RGBA (interpolated)
	glm::vec4 end;      ///< end color RGBA
	glm::vec4 misc;     ///< x = lifeRemaining, y = lifeMax, z = seed, w = endSize
	glm::vec4 extra;    ///< x = startSize, y = rotationSpeed, z = drag, w = emitterIndex
};

/// @brief Individual particle emitter with its own GPU resources
class ParticleEmitter {
public:
	ParticleEmitter();
	~ParticleEmitter();

	ParticleEmitter(const ParticleEmitter&) = delete;
	ParticleEmitter& operator=(const ParticleEmitter&) = delete;
	ParticleEmitter(ParticleEmitter&& other) noexcept;
	ParticleEmitter& operator=(ParticleEmitter&& other) noexcept;

	/// @brief Initialize GPU resources for this emitter
	void InitGPUResources(const std::shared_ptr<renderer::Shader>& computeShader, const std::shared_ptr<renderer::Shader>& renderShader);

	/// @brief Cleanup GPU resources
	void CleanupGPUResources();

	/// @brief Reinitialize GPU buffers
	void ReinitializeBuffers();

	/// @brief Update and render this emitter's particles
	void UpdateAndRender(
	    const glm::mat4& viewProjection, const glm::vec3& worldPos, const glm::mat3& parentRotation, const glm::vec3& camRight, const glm::vec3& camUp,
	    float deltaTime
	);

	/// @brief Spawn particles for this emitter
	void SpawnParticles(uint32_t count, const glm::vec3& worldPos, const glm::mat3& parentRotation);

	/// @brief Play/resume emission
	void Play() {
		m_isPlaying = true;
	}

	/// @brief Pause emission
	void Pause() {
		m_isPlaying = false;
	}

	/// @brief Stop and clear all particles
	void Stop();

	/// @brief Emit a burst
	void EmitBurst(uint32_t count, const glm::vec3& worldPos, const glm::mat3& parentRotation) {
		SpawnParticles(count, worldPos, parentRotation);
	}

	/// @brief Check if playing
	[[nodiscard]]
	bool IsPlaying() const {
		return m_isPlaying;
	}

	/// @brief Get particle count
	[[nodiscard]]
	uint32_t GetParticleCount() const {
		return m_aliveCount;
	}

	/// @brief Load texture based on config
	void LoadTexture();

	/// @brief Get/set configuration
	ParticleEmitterConfig& GetConfig() {
		return m_config;
	}

	const ParticleEmitterConfig& GetConfig() const {
		return m_config;
	}

	/// @brief Check if GPU is initialized
	[[nodiscard]]
	bool IsGPUInitialized() const {
		return m_gpuInitialized;
	}

private:
	/// @brief Generate spawn position based on shape
	[[nodiscard]]
	glm::vec3 GenerateSpawnPosition(const glm::mat3& rotation);

	/// @brief Generate spawn velocity
	[[nodiscard]]
	glm::vec3 GenerateSpawnVelocity(const glm::mat3& rotation);

	/// @brief Random float in range
	[[nodiscard]]
	float RandomFloat(float min, float max);

	/// @brief Random direction on unit sphere
	[[nodiscard]]
	glm::vec3 RandomDirection();

	ParticleEmitterConfig m_config;

	// Playback state
	bool m_isPlaying = true;
	bool m_gpuInitialized = false;
	float m_systemTime = 0.0f;
	float m_emissionAccumulator = 0.0f;
	uint32_t m_aliveCount = 0;

	// GPU resources
	GLuint m_particleBuffers[2] = { 0, 0 };
	GLuint m_counterBuffer = 0;
	uint32_t* m_counterBufferPtr = nullptr;
	GLuint m_frameParamsUBO = 0;
	int m_currentBuffer = 0;

	// Shared shaders
	std::shared_ptr<renderer::Shader> m_computeShader;
	std::shared_ptr<renderer::Shader> m_renderShader;

	// Per-emitter texture
	std::shared_ptr<Texture> m_texture;

	// RNG
	std::mt19937 m_rng;
	std::uniform_real_distribution<float> m_dist { 0.0f, 1.0f };
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

	/// @brief Load particle system config from a Lua file
	/// @param luaPath Path to the Lua config
	/// @return true if loaded successfully
	bool LoadFromLua(const std::string& luaPath);

	/// @brief Save particle system config to a Lua file
	/// @param luaPath Path to save the config
	/// @return true if saved successfully
	bool SaveToLua(const std::string& luaPath) const;

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

	// Particle System Control

	/// @brief Start/resume all emitters
	void Play();

	/// @brief Pause all emitters
	void Pause();

	/// @brief Stop and clear all particles
	void Stop();

	/// @brief Emit a burst from all emitters
	void EmitBurst(uint32_t count);

	/// @brief Check if the system is playing
	[[nodiscard]]
	bool IsPlaying() const {
		return m_isPlaying;
	}

	/// @brief Get total particle count across all emitters
	[[nodiscard]]
	uint32_t GetParticleCount() const;

	// Emitter Management

	/// @brief Add a new emitter
	/// @return Reference to the new emitter
	ParticleEmitter& AddEmitter();

	/// @brief Add a new emitter with a preset
	/// @param presetName Name of the preset
	/// @return Reference to the new emitter
	ParticleEmitter& AddEmitterWithPreset(const std::string& presetName);

	/// @brief Remove an emitter by index
	void RemoveEmitter(size_t index);

	/// @brief Get emitter count
	[[nodiscard]]
	size_t GetEmitterCount() const {
		return m_emitters.size();
	}

	/// @brief Get emitter by index
	ParticleEmitter& GetEmitter(size_t index) {
		return m_emitters[index];
	}

	const ParticleEmitter& GetEmitter(size_t index) const {
		return m_emitters[index];
	}

	/// @brief Get all emitters
	std::vector<ParticleEmitter>& GetEmitters() {
		return m_emitters;
	}

	const std::vector<ParticleEmitter>& GetEmitters() const {
		return m_emitters;
	}

	/// @brief Get/set Lua config path
	const std::string& GetLuaConfigPath() const {
		return m_luaConfigPath;
	}

	void SetLuaConfigPath(const std::string& path) {
		m_luaConfigPath = path;
	}

private:
	/// @brief Initialize shared resources
	void InitSharedResources();

	/// @brief Cleanup shared resources
	void CleanupSharedResources();

	// IRenderable implementation
	void OnRender(const glm::mat4& viewProjection) noexcept override;

	// Emitters
	std::vector<ParticleEmitter> m_emitters;

	// Shared resources
	std::shared_ptr<renderer::Shader> m_computeShader;
	std::shared_ptr<renderer::Shader> m_renderShader;
	GLuint m_quadVAO = 0;
	GLuint m_quadVBO = 0;

	bool m_isPlaying = true;
	bool m_sharedResourcesInitialized = false;

	// Serialization
	std::string m_luaConfigPath;    ///< Path to Lua config file

	// Culling
	int m_cullingRadius = 20;
};

}    // namespace toast
