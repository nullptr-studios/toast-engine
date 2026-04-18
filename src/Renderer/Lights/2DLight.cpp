/// @file 2DLight.cpp
/// @author dario
/// @date 22/11/2025.

#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Renderer/OpenGL/GLStateCache.hpp"
#include "Toast/Resources/ResourceManager.hpp"
#include "Toast/Resources/Texture.hpp"
#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

#include "Toast/Renderer/Lights/2DLight.hpp"
#include "Toast/Renderer/OclussionVolume.hpp"

#include <algorithm>

void LightRenderable::OnRender(renderer::IRenderablePass pass, const glm::mat4& viewProjection) noexcept {
	// yes yes i know, why is this here? for simplicity sakes and correct transparency, having in another render pass would break the light blending
	// with transparents
	if (pass == renderer::IRenderablePass::GEOMETRY) {
		m_light->OnRender(viewProjection);
	}
}

float LightRenderable::GetDepth() noexcept {
	return m_light->transform()->worldPosition().z;
}

void Light2D::Init() {
	// Load quad mesh for light rendering
	m_lightMesh = resource::LoadResource<renderer::Mesh>("assets/MODELS/quad.obj");
	m_lightShader = resource::LoadResource<renderer::Shader>("assets/SHADERS/2dLight.shader");

	transform()->scale(glm::vec3(m_radius * 2, m_radius * 2, 1.0f));
	m_renderable = new LightRenderable(this);
}

void Light2D::LoadTextures() {
	Actor::LoadTextures();
	renderer::IRendererBase::GetInstance()->AddLight(this);
	// renderer::IRendererBase::GetInstance()->AddRenderable(m_renderable);
	// m_lightBuffer = renderer::IRendererBase::GetInstance()->GetLightFramebuffer();
}

void Light2D::Begin() { }

void Light2D::Destroy() {
	renderer::IRendererBase::GetInstance()->RemoveLight(this);
	// renderer::IRendererBase::GetInstance()->RemoveRenderable(m_renderable);
	delete m_renderable;
}

void Light2D::OnRender(const glm::mat4& premultiplied_matrix) const {
	if (!enabled()) {
		return;
	}

	// Culling
	if (!OclussionVolume::isSphereOnPlanes(transform()->worldPosition(), m_radius)) {
		return;
	}

	// Save previous GL state
	GLboolean prevDepthMask = GL_TRUE;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);

	GLint prevBlendSrc = GL_SRC_ALPHA, prevBlendDst = GL_ONE_MINUS_SRC_ALPHA;
	glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrc);
	glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDst);

	// Setup additive light blending
	renderer::SetDepthMask(false);
	renderer::SetBlend(true);
	renderer::SetBlendFunc(GL_SRC_ALPHA, GL_ONE);
	renderer::SetDepthTest(true);

	auto model = transform()->GetWorldMatrix();
	auto mvp = premultiplied_matrix * model;

	m_lightShader->Use();
	m_lightShader->Set("gMVP", mvp);

	// Bind SDF shadow texture
	Texture::BindTextureId(0, renderer::IRendererBase::GetInstance()->GetShadowMapTexture());
	m_lightShader->Set("sdfTex", 0);

	auto* geometryFb = renderer::IRendererBase::GetInstance()->GetLightFramebuffer();
	if (geometryFb) {
		const int width = geometryFb->Width();
		const int height = geometryFb->Height();

		m_lightShader->Set("screenSize", glm::ivec2(width, height));

		const glm::vec2 lightPosUV = TransformToUV(transform()->worldPosition(), premultiplied_matrix);

		const glm::vec3 lightEdgeWorld = glm::vec3(model * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

		const glm::vec2 lightEdgeUV = TransformToUV(lightEdgeWorld, premultiplied_matrix);

		const float lightRadiusUV = std::max(0.0001f, glm::distance(lightPosUV, lightEdgeUV));

		glm::vec2 lightDirUV = lightEdgeUV - lightPosUV;
		if (glm::dot(lightDirUV, lightDirUV) < 1e-8f) {
			lightDirUV = glm::vec2(1.0f, 0.0f);
		} else {
			lightDirUV = glm::normalize(lightDirUV);
		}

		const float aspect = static_cast<float>(width) / static_cast<float>(height);

		glm::vec2 correctedLightDir = glm::normalize(glm::vec2(lightDirUV.x * aspect, lightDirUV.y));

		const float angleRad = glm::radians(m_angle);
		const float cosOuter = std::cos(angleRad);
		const float cosInner = std::cos(std::max(angleRad - m_angularSoftness, 0.0f));

		const float invLightRadius = 1.0f / lightRadiusUV;
		const float invShadowRadius = 1.0f / std::max(m_lightShadowRadius, 0.0001f);

		m_lightShader->Set("lightPosUV", lightPosUV);
		m_lightShader->Set("invLightRadius", invLightRadius);
		m_lightShader->Set("correctedLightDir", correctedLightDir);

		m_lightShader->Set("cosOuter", cosOuter);
		m_lightShader->Set("cosInner", cosInner);

		m_lightShader->Set("invShadowRadius", invShadowRadius);

		m_lightShader->Set("gShadowSteps", static_cast<int>(renderer::IRendererBase::GetInstance()->GetRendererConfig().shadowRaymarchSteps));

		m_lightShader->Set("doShadows", m_castShadow);

		m_lightShader->Set("gLightColor", m_color);
		m_lightShader->Set("gLightIntensity", m_intensity);
		m_lightShader->Set("gLightVolumetricIntensity", m_volumetricIntensity);
		m_lightShader->Set("gRadialSoftness", m_radialSoftness);
		m_lightShader->Set("gAngularSoftness", m_angularSoftness);
	}

	// Draw light volume
	m_lightMesh->Draw();

	// Restore GL state
	renderer::SetBlendFunc(static_cast<GLenum>(prevBlendSrc), static_cast<GLenum>(prevBlendDst));
	renderer::SetCullFace(false);
	renderer::SetBlend(true);
	renderer::SetDepthTest(true);
	renderer::SetDepthMask(prevDepthMask ? GL_TRUE : GL_FALSE);
	renderer::SetDepthFunc(GL_LEQUAL);
	glClearDepth(1.0);
}

json_t Light2D::Save() const {
	json_t j = Actor::Save();
	j["radius"] = m_radius;
	j["intensity"] = m_intensity;
	j["volumetric_intensity"] = m_volumetricIntensity;
	j["angle"] = m_angle;
	j["radial_softness"] = m_radialSoftness;
	j["angular_softness"] = m_angularSoftness;
	j["color"] = { m_color.r, m_color.g, m_color.b, m_color.a };
	j["light_shadow_radius"] = m_lightShadowRadius;
	j["cast_shadow"] = m_castShadow;
	return j;
}

void Light2D::Load(json_t j, bool force_create) {
	Actor::Load(j, force_create);
	if (j.contains("radius")) {
		m_radius = j.at("radius").get<float>();
	}
	if (j.contains("intensity")) {
		m_intensity = j.at("intensity").get<float>();
	}
	if (j.contains("volumetric_intensity")) {
		m_volumetricIntensity = j.at("volumetric_intensity").get<float>();
	}
	if (j.contains("angle")) {
		m_angle = j.at("angle").get<float>();
	}
	if (j.contains("radial_softness")) {
		m_radialSoftness = j.at("radial_softness").get<float>();
	}
	if (j.contains("angular_softness")) {
		m_angularSoftness = j.at("angular_softness").get<float>();
	}
	if (j.contains("light_shadow_radius")) {
		m_lightShadowRadius = j.at("light_shadow_radius").get<float>();
	}
	if (j.contains("cast_shadow")) {
		m_castShadow = j.at("cast_shadow").get<bool>();
	}

	if (j.contains("color")) {
		auto color_array = j.at("color").get<std::vector<float>>();
		if (color_array.size() == 4) {
			m_color = glm::vec4(color_array[0], color_array[1], color_array[2], color_array[3]);
		}
	}
}

#ifdef TOAST_EDITOR
void Light2D::Inspector() {
	Actor::Inspector();

	ImGui::DragFloat("Light Radius", &m_radius, 0.5f, 0.0f, 10000.0f);
	ImGui::DragFloat("Light Intensity", &m_intensity, 0.05f, 0.0f, 1.0f);
	ImGui::DragFloat("Light Volumetric Intensity", &m_volumetricIntensity, 0.05f, 0.0f, 1.0f);
	ImGui::DragFloat("Light Angle", &m_angle, 1.0f, 0.0f, 180.0f);
	ImGui::DragFloat("Radial Softness", &m_radialSoftness, 0.01f, 0.001f, .25f);
	ImGui::DragFloat("Angular Softness", &m_angularSoftness, 0.01f, 0.000f, 1.0f);
	ImGui::DragFloat("Light Shadow Radius", &m_lightShadowRadius, 0.5f, 0.05f, 10.0f);
	ImGui::Checkbox("Cast Shadows", &m_castShadow);

	ImGui::ColorEdit4("Light Color", &m_color.r);

	transform()->scale(glm::vec3(m_radius * 2, m_radius * 2, 1.0f));
}
#endif
