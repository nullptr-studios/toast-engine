/**
 * @file fog_volume.hpp
 * @author dario
 * @date 26/10/2026
 */

#pragma once
#include "node_3d.hpp"

#include <glm/glm.hpp>
#include <toast/export.hpp>

namespace toast {

class [[ToastNode, Icon("FogVolume")]] TOAST_API FogVolume : public Node3D {
public:
	FogVolume() = default;
	~FogVolume() override = default;

	[[nodiscard]]
	auto isEnabled() const noexcept -> bool {
		return m_enabled;
	}

	[[nodiscard]]
	auto extents() const noexcept -> const glm::vec3& {
		return m_extents;
	}

	[[nodiscard]]
	auto edgeFade() const noexcept -> float {
		return m_edge_fade;
	}

	[[nodiscard]]
	auto density() const noexcept -> float {
		return m_density;
	}

	[[nodiscard]]
	auto albedo() const noexcept -> const glm::vec3& {
		return m_albedo;
	}

	/// Radiance per meter at density 1
	[[nodiscard]]
	auto emission() const noexcept -> glm::vec3 {
		return m_emission * m_emission_intensity;
	}

	[[nodiscard]]
	auto anisotropy() const noexcept -> float {
		return m_anisotropy;
	}

	[[nodiscard]]
	auto ambient() const noexcept -> float {
		return m_ambient;
	}

	[[nodiscard]]
	auto noiseStrength() const noexcept -> float {
		return m_noise_strength;
	}

	[[nodiscard]]
	auto noiseScale() const noexcept -> float {
		return m_noise_scale;
	}

	[[nodiscard]]
	auto wind() const noexcept -> const glm::vec3& {
		return m_wind;
	}

	void density(float value) noexcept { m_density = value; }

private:
	void init();
	void end();
	void destroy();

	[[Reflect, Name("Enabled")]]
	bool m_enabled = true;

	[[Reflect, Name("Extents"), Unit("m"), Group("Shape")]] alignas(16) glm::vec3 m_extents = glm::vec3(2.0f);
	[[Reflect, Name("Edge Fade"), Unit("m"), Range(0.0, 10.0), Group("Shape")]]
	float m_edge_fade = 0.5f;

	[[Reflect, Name("Density"), Unit("1/m"), Range(0.0, 10.0), Group("Medium")]]
	float m_density = 0.2f;
	[[Reflect, Name("Albedo"), Color, Group("Medium")]]
	glm::vec3 m_albedo = glm::vec3(0.8f);
	[[Reflect, Name("Emission"), Color, Group("Medium")]]
	glm::vec3 m_emission = glm::vec3(1.0f, 0.45f, 0.15f);
	[[Reflect, Name("Emission Intensity"), Range(0.0, 100.0), Group("Medium")]]
	float m_emission_intensity = 0.0f;
	[[Reflect, Name("Anisotropy"), Range(-0.9, 0.9), Group("Medium")]]
	float m_anisotropy = 0.2f;
	[[Reflect, Name("Ambient"), Range(0.0, 4.0), Group("Medium")]]
	float m_ambient = 1.0f;

	[[Reflect, Name("Noise Strength"), Range(0.0, 1.0), Group("Noise")]]
	float m_noise_strength = 0.0f;
	[[Reflect, Name("Noise Scale"), Unit("m"), Range(0.05, 50.0), Group("Noise")]]
	float m_noise_scale = 2.0f;
	[[Reflect, Name("Wind"), Unit("m/s"), Group("Noise")]] alignas(16) glm::vec3 m_wind = glm::vec3(0.0f);

	bool m_registered_proxy = false;
};

}
