#define GLM_ENABLE_EXPERIMENTAL

#include "Engine/Toast/Objects/Actor.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/gtx/quaternion.hpp"

#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

#include "Engine/Renderer/IRendererBase.hpp"

#include <Engine/Core/GlmJson.hpp>
#include <Engine/Toast/Components/TransformComponent.hpp>

using namespace glm;

namespace toast {

static constexpr float kEPS = 1e-6f;

glm::vec3 TransformComponent::SafeCompDiv(const glm::vec3& a, const glm::vec3& b) noexcept {
	return { std::abs(b.x) > kEPS ? a.x / b.x : 0.0f, std::abs(b.y) > kEPS ? a.y / b.y : 0.0f, std::abs(b.z) > kEPS ? a.z / b.z : 0.0f };
}

TransformComponent::TransformComponent()
    : m_position(0.0f),
      m_rotation(identity<quat>()),
      m_scale(1.0f),
      m_cachedMatrix(identity<mat4>()),
      m_cachedInverse(identity<mat4>()),
      m_cachedWorldMatrix(identity<mat4>()) {
	m_eulerDegreesCache = degrees(eulerAngles(m_rotation));
	m_eulerCacheValid = true;
	m_cachedParentWorldPos = glm::vec3(0.0f);
	m_cachedParentWorldRot = glm::identity<glm::quat>();
	m_cachedParentWorldScl = glm::vec3(1.0f);
}

TransformComponent::TransformComponent(const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& scale)
    : m_position(pos),
      m_rotation(quat(radians(rot))),
      m_scale(scale),
      m_cachedMatrix(identity<mat4>()),
      m_cachedInverse(identity<mat4>()),
      m_cachedWorldMatrix() {
	m_eulerDegreesCache = rot;
	m_eulerCacheValid = true;
	m_cachedParentWorldPos = glm::vec3(0.0f);
	m_cachedParentWorldRot = glm::identity<glm::quat>();
	m_cachedParentWorldScl = glm::vec3(1.0f);
}

json_t TransformComponent::Save() const {
	json_t j = Component::Save();
	j["position"] = m_position;
	j["rotation"] = m_rotation;
	j["scale"] = m_scale;

	return j;
}

void TransformComponent::Load(json_t j, bool force_create) {
	Component::Load(j);
	if (j.contains("position")) {
		m_position = j.at("position");
	}
	if (j.contains("rotation")) {
		m_rotation = j.at("rotation");
	}
	if (j.contains("scale")) {
		m_scale = j.at("scale");
	}

	// Refresh caches
	m_dirtyMatrix = m_dirtyInverse = m_dirtyWorldMatrix = m_dirtyDirectionVectors = true;
	m_eulerDegreesCache = degrees(eulerAngles(m_rotation));
	m_eulerCacheValid = true;

	UpdateChildrenWorldMatrix();
}

#ifdef TOAST_EDITOR
void TransformComponent::Inspector() {
	Component::Inspector();

	// Use a persistent Euler cache for editing to avoid re-deriving from quaternion each frame,
	// which can clamp around +/-90° due to Euler ambiguity.
	if (!m_eulerCacheValid) {
		m_eulerDegreesCache = degrees(eulerAngles(m_rotation));
		m_eulerCacheValid = true;
	}

	bool changed = false;

	ImGui::PushID(this);
	changed |= ImGui::DragFloat3("Position", &m_position.x, 0.1f);

	// Edit cached euler, then rebuild quaternion only if changed
	glm::vec3 eulerDeg = m_eulerDegreesCache;
	if (ImGui::DragFloat3("Rotation (deg)", &eulerDeg.x, 0.1f)) {
		m_eulerDegreesCache = eulerDeg;
		m_rotation = glm::normalize(glm::quat(glm::radians(m_eulerDegreesCache)));
		m_dirtyDirectionVectors = true;
		changed = true;
	}

	changed |= ImGui::DragFloat3("Scale", &m_scale.x, 0.1f);

	if (changed) {
		m_dirtyMatrix = m_dirtyInverse = m_dirtyWorldMatrix = m_dirtyDirectionVectors = true;
		UpdateChildrenWorldMatrix();
	}
	ImGui::PopID();
}
#endif

glm::vec3 TransformComponent::position() const noexcept {
	return m_position;
}

glm::vec3 TransformComponent::rotation() const noexcept {
	return degrees(eulerAngles(m_rotation));
}

glm::vec3 TransformComponent::rotationRadians() const noexcept {
	return eulerAngles(m_rotation);
}

glm::quat TransformComponent::rotationQuat() const noexcept {
	return m_rotation;
}

glm::vec3 TransformComponent::scale() const noexcept {
	return m_scale;
}

void TransformComponent::position(const glm::vec3& position) noexcept {
	m_position = position;
	m_dirtyMatrix = m_dirtyInverse = m_dirtyWorldMatrix = true;
	UpdateChildrenWorldMatrix();
}

void TransformComponent::rotation(const glm::vec3& degreesVal) noexcept {
	m_rotation = normalize(quat(radians(degreesVal)));
	m_eulerDegreesCache = degreesVal;    // keep editor cache in sync to avoid 90° lock
	m_eulerCacheValid = true;
	m_dirtyMatrix = m_dirtyInverse = m_dirtyWorldMatrix = m_dirtyDirectionVectors = true;
	UpdateChildrenWorldMatrix();
}

void TransformComponent::rotationRadians(const glm::vec3& rotationVal) noexcept {
	m_rotation = normalize(quat(rotationVal));
	m_eulerDegreesCache = degrees(rotationVal);
	m_eulerCacheValid = true;
	m_dirtyMatrix = m_dirtyInverse = m_dirtyWorldMatrix = m_dirtyDirectionVectors = true;
	UpdateChildrenWorldMatrix();
}

void TransformComponent::rotationQuat(const glm::quat& quaternion) noexcept {
	m_rotation = normalize(quaternion);
	m_eulerDegreesCache = degrees(eulerAngles(m_rotation));
	m_eulerCacheValid = true;
	m_dirtyMatrix = m_dirtyInverse = m_dirtyWorldMatrix = m_dirtyDirectionVectors = true;
	UpdateChildrenWorldMatrix();
}

void TransformComponent::scale(const glm::vec3& scaleVal) noexcept {
	m_scale = scaleVal;
	m_dirtyMatrix = m_dirtyInverse = m_dirtyWorldMatrix = true;
	UpdateChildrenWorldMatrix();
}

// -------- World TRS helpers --------

void TransformComponent::ComputeParentWorldTRS(glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScl) const noexcept {
	// If world matrix is clean, reuse cached parent TRS
	if (!m_dirtyWorldMatrix) {
		outPos = m_cachedParentWorldPos;
		outRot = m_cachedParentWorldRot;
		outScl = m_cachedParentWorldScl;
		return;
	}

	// Recompute and update cache when world is dirty
	glm::vec3 accPos = glm::vec3(0.0f);
	glm::quat accRot = glm::identity<glm::quat>();
	glm::vec3 accScl = glm::vec3(1.0f);

	Object* object_ptr = parent();
	while (object_ptr) {
		TransformComponent* parent_transform = nullptr;

		if (Actor* parent_actor = dynamic_cast<Actor*>(object_ptr)) {
			// Skip using our own transform as parent if parent is our owning Actor
			if (parent_actor->transform() == this) {
				object_ptr = object_ptr->parent();
				continue;
			}
			parent_transform = parent_actor->transform();
		} else if (TransformComponent* parent_comp = dynamic_cast<TransformComponent*>(object_ptr)) {
			parent_transform = parent_comp;
		} else {
			// Skip non-transform parents
			object_ptr = object_ptr->parent();
			continue;
		}

		if (parent_transform) {
			accPos = parent_transform->rotationQuat() * (accPos * parent_transform->scale()) + parent_transform->position();
			accRot = parent_transform->rotationQuat() * accRot;
			accScl *= parent_transform->scale();
		}

		object_ptr = object_ptr->parent();
	}

	// Update cache and output
	m_cachedParentWorldPos = accPos;
	m_cachedParentWorldRot = accRot;
	m_cachedParentWorldScl = accScl;

	outPos = accPos;
	outRot = accRot;
	outScl = accScl;
}

// -------- World TRS getters --------

glm::vec3 TransformComponent::worldPosition() noexcept {
	glm::vec3 pPos;
	glm::quat pRot;
	glm::vec3 pScl;
	ComputeParentWorldTRS(pPos, pRot, pScl);
	return pRot * (m_position * pScl) + pPos;
}

glm::quat TransformComponent::worldRotationQuat() noexcept {
	glm::vec3 pPos;
	glm::quat pRot;
	glm::vec3 pScl;
	ComputeParentWorldTRS(pPos, pRot, pScl);
	return normalize(pRot * m_rotation);
}

glm::vec3 TransformComponent::worldRotationRadians() noexcept {
	return eulerAngles(worldRotationQuat());
}

glm::vec3 TransformComponent::worldRotation() noexcept {
	return degrees(worldRotationRadians());
}

glm::vec3 TransformComponent::worldScale() noexcept {
	glm::vec3 pPos;
	glm::quat pRot;
	glm::vec3 pScl;
	ComputeParentWorldTRS(pPos, pRot, pScl);
	return m_scale * pScl;
}

// -------- World TRS setters --------

void TransformComponent::worldPosition(const glm::vec3& worldPos) noexcept {
	// localPos = inverse(pRot) * ((worldPos - pPos) / pScl)
	glm::vec3 pPos;
	glm::quat pRot;
	glm::vec3 pScl;
	ComputeParentWorldTRS(pPos, pRot, pScl);

	const glm::quat invPR = inverse(pRot);
	const glm::vec3 localPos = invPR * SafeCompDiv(worldPos - pPos, pScl);

	position(localPos);    // will mark dirties and propagate
}

void TransformComponent::worldRotationQuat(const glm::quat& worldRot) noexcept {
	glm::vec3 pPos;
	glm::quat pRot;
	glm::vec3 pScl;
	ComputeParentWorldTRS(pPos, pRot, pScl);

	const glm::quat localRot = normalize(inverse(pRot) * worldRot);
	rotationQuat(localRot);    // marks dirties and syncs editor cache
}

void TransformComponent::worldRotationRadians(const glm::vec3& worldRotRadians) noexcept {
	worldRotationQuat(normalize(quat(worldRotRadians)));
}

void TransformComponent::worldRotation(const glm::vec3& worldRotDegrees) noexcept {
	worldRotationQuat(normalize(quat(radians(worldRotDegrees))));
}

void TransformComponent::worldScale(const glm::vec3& worldScl) noexcept {
	glm::vec3 pPos;
	glm::quat pRot;
	glm::vec3 pScl;
	ComputeParentWorldTRS(pPos, pRot, pScl);

	const glm::vec3 localScl = SafeCompDiv(worldScl, pScl);
	scale(localScl);    // marks dirties and propagates
}

// -------- Direction vectors --------

glm::vec3 TransformComponent::GetFrontVector() noexcept {
	if (!m_dirtyDirectionVectors) {
		return m_front;
	}
	CalcDirectionVectors();
	return m_front;
}

glm::vec3 TransformComponent::GetRightVector() noexcept {
	if (!m_dirtyDirectionVectors) {
		return m_right;
	}
	CalcDirectionVectors();
	return m_right;
}

glm::vec3 TransformComponent::GetUpVector() noexcept {
	if (!m_dirtyDirectionVectors) {
		return m_up;
	}
	CalcDirectionVectors();
	return m_up;
}

// -------- Matrices --------

glm::mat4 TransformComponent::GetMatrix() noexcept {
	if (!m_dirtyMatrix) {
		return m_cachedMatrix;
	}

	// T*R*S
	mat4 pos_mat = translate(identity<mat4>(), m_position);
	mat4 rot_mat = mat4_cast(m_rotation);
	mat4 scl_mat = glm::scale(identity<mat4>(), m_scale);
	m_cachedMatrix = pos_mat * rot_mat * scl_mat;

	m_dirtyMatrix = false;
	return m_cachedMatrix;
}

glm::mat4 TransformComponent::GetInverse() noexcept {
	if (!m_dirtyInverse) {
		return m_cachedInverse;
	}

	constexpr float EPSILON = 1e-6f;
	glm::vec3 inv_scale { std::abs(m_scale.x) > EPSILON ? 1.0f / m_scale.x : 0.0f,
		                    std::abs(m_scale.y) > EPSILON ? 1.0f / m_scale.y : 0.0f,
		                    std::abs(m_scale.z) > EPSILON ? 1.0f / m_scale.z : 0.0f };

	// If any inverse scale is zero, ignore position along that axis to avoid inf in translation
	glm::vec3 safe_position { inv_scale.x != 0.0f ? -m_position.x : 0.0f,
		                        inv_scale.y != 0.0f ? -m_position.y : 0.0f,
		                        inv_scale.z != 0.0f ? -m_position.z : 0.0f };

	// S*R*T
	mat4 pos_mat = translate(identity<mat4>(), safe_position);
	mat4 rot_mat = mat4_cast(inverse(m_rotation));
	mat4 scl_mat = glm::scale(identity<mat4>(), inv_scale);
	m_cachedInverse = scl_mat * rot_mat * pos_mat;

	m_dirtyInverse = false;
	return m_cachedInverse;
}

glm::mat4 TransformComponent::GetWorldMatrix() noexcept {
	if (!m_dirtyWorldMatrix) {
		return m_cachedWorldMatrix;
	}

	// If no transform-bearing parent, world == local
	if (!parent()) {
		m_cachedWorldMatrix = GetMatrix();
		m_dirtyWorldMatrix = false;
		return m_cachedWorldMatrix;
	}

	glm::vec3 world_pos = m_position;
	glm::quat world_rot = m_rotation;
	glm::vec3 world_scl = m_scale;

	Object* object_ptr = parent();
	while (object_ptr) {
		TransformComponent* parent_transform = nullptr;

		if (Actor* parent_actor = dynamic_cast<Actor*>(object_ptr)) {
			if (parent_actor->transform() == this) {
				object_ptr = object_ptr->parent();
				continue;
			}
			parent_transform = parent_actor->transform();
		} else if (TransformComponent* parent_comp = dynamic_cast<TransformComponent*>(object_ptr)) {
			parent_transform = parent_comp;
		} else {
			// Skip non-transform parents
			object_ptr = object_ptr->parent();
			continue;
		}

		if (parent_transform) {
			// world_child = T_parent * R_parent * S_parent applied to local/world accumulator
			world_pos = parent_transform->rotationQuat() * (world_pos * parent_transform->scale()) + parent_transform->position();
			world_rot = parent_transform->rotationQuat() * world_rot;
			world_scl *= parent_transform->scale();
		}

		object_ptr = object_ptr->parent();
	}

	mat4 pos_mat = translate(identity<mat4>(), world_pos);
	mat4 rot_mat = mat4_cast(world_rot);
	mat4 scl_mat = glm::scale(identity<mat4>(), world_scl);
	m_cachedWorldMatrix = pos_mat * rot_mat * scl_mat;

	m_dirtyWorldMatrix = false;
	return m_cachedWorldMatrix;
}

void TransformComponent::CalcDirectionVectors() {
	glm::vec3 forward_local(0.0f, 0.0f, -1.0f);
	glm::vec3 right_local(1.0f, 0.0f, 0.0f);
	glm::vec3 up_local(0.0f, 1.0f, 0.0f);

	m_front = glm::normalize(m_rotation * forward_local);
	m_right = glm::normalize(m_rotation * right_local);
	m_up = glm::normalize(m_rotation * up_local);

	m_dirtyDirectionVectors = false;
}

void TransformComponent::UpdateChildrenWorldMatrix() {
	auto begin = children.begin();
	auto end = children.end();
	if (m_attachedActor) {
		begin = m_attachedActor->children.begin();
		end = m_attachedActor->children.end();
	}
	for (auto child = begin; child != end; ++child) {
		if (auto a = dynamic_cast<Actor*>(child->second.get())) {
			if (auto* t = a->transform()) {
				t->m_dirtyWorldMatrix = true;
			}
		} else if (auto t = dynamic_cast<TransformComponent*>(child->second.get())) {
			t->m_dirtyWorldMatrix = true;
		} else if (auto r = dynamic_cast<IRenderable*>(child->second.get())) {
			r->m_dirtyWorldMatrix = true;
		}
	}
}

}    // namespace toast
