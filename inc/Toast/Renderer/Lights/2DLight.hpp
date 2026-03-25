/// @file 2DLight.hpp
/// @author dario
/// @date 22/11/2025.

#pragma once
#include "Toast/Objects/Actor.hpp"
#include "Toast/Renderer/IRenderable.hpp"
#include "Toast/Renderer/Shader.hpp"
#include "Toast/Resources/Mesh.hpp"

class Framebuffer;
class Light2D;

class LightRenderable : public renderer::IRenderable {
public:
	LightRenderable(Light2D* light) : m_light(light) { }

	void OnRender(renderer::IRenderablePass pass, const glm::mat4& viewProjection) noexcept override;

	float GetDepth() noexcept override;

private:
	Light2D* m_light;
};

class Light2D : public toast::Actor {
public:
	REGISTER_TYPE(Light2D);

	void Init() override;
	void LoadTextures() override;
	void Begin() override;

	void Destroy() override;

	void OnRender(const glm::mat4& premultiplied_matrix) const;

	json_t Save() const override;
	void Load(json_t j, bool force_create = true) override;

	void SetRadius(float radius) {
		m_radius = radius;
		transform()->scale(glm::vec3(m_radius, m_radius, 1.0f));
	}

	float GetRadius() const {
		return m_radius;
	}

	void SetIntensity(float intensity) {
		m_intensity = intensity;
	}

	float GetIntensity() const {
		return m_intensity;
	}

	void SetVolumetricIntensity(float intensity) {
		m_volumetricIntensity = intensity;
	}

	float GetVolumetricIntensity() const {
		return m_volumetricIntensity;
	}

	void SetAngle(float angle) {
		m_angle = angle;
	}

	float GetAngle() const {
		return m_angle;
	}

	void SetColor(const glm::vec4& color) {
		m_color = color;
	}

	glm::vec4 GetColor() const {
		return m_color;
	}

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

private:
	glm::vec2 TransformToUV(const glm::vec3& worldPos, const glm::mat4& viewProj) const {
		glm::vec4 clipSpace = viewProj * glm::vec4(worldPos, 1.0f);

		glm::vec3 ndc = glm::vec3(clipSpace) / clipSpace.w;

		glm::vec2 uv;
		uv.x = ndc.x * 0.5f + 0.5f;
		uv.y = ndc.y * 0.5f + 0.5f;

		return uv;
	}

	std::shared_ptr<renderer::Mesh> m_lightMesh = nullptr;
	std::shared_ptr<renderer::Shader> m_lightShader = nullptr;

	// Framebuffer* m_lightBuffer = nullptr;

	glm::vec4 m_color = glm::vec4(1.0f);
	float m_intensity = 1.0f;
	float m_volumetricIntensity = 0.5f;
	float m_angle = 180.0f;
	float m_radius = 15.0f;

	float m_radialSoftness = 0.25f;
	float m_angularSoftness = 0.5f;

	bool m_castShadow = true;
	float m_lightShadowRadius = 0.05f;

	IRenderable* m_renderable = nullptr;
};
