/// @file TransformComponent.hpp
/// @author Xein
/// @date 28/05/25
/// @brief Contains the Transform2D and Transform 3D objects

#pragma once
#include "Component.hpp"
#include "Engine/Toast/Objects/Object.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace toast {

class TransformComponent : public Component {
public:
	REGISTER_ABSTRACT(TransformComponent)
	TransformComponent();
	TransformComponent(const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& scale);
	~TransformComponent() override = default;

	// Serialize
	[[nodiscard]]
	json_t Save() const override;
	void Load(json_t j, bool force_create = true) override;

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

	// Local TRS
	[[nodiscard]]
	glm::vec3 position() const noexcept;
	void position(const glm::vec3& position) noexcept;

	[[nodiscard]]
	glm::vec3 rotation() const noexcept;
	void rotation(const glm::vec3& degrees) noexcept;
	[[nodiscard]]
	glm::vec3 rotationRadians() const noexcept;
	void rotationRadians(const glm::vec3& rotation) noexcept;
	[[nodiscard]]
	glm::quat rotationQuat() const noexcept;
	void rotationQuat(const glm::quat& quaternion) noexcept;

	[[nodiscard]]
	glm::vec3 scale() const noexcept;
	void scale(const glm::vec3& scale) noexcept;

	[[nodiscard]]
	glm::vec3 worldPosition() noexcept;
	[[nodiscard]]
	glm::quat worldRotationQuat() noexcept;
	[[nodiscard]]
	glm::vec3 worldRotationRadians() noexcept;
	[[nodiscard]]
	glm::vec3 worldRotation() noexcept;
	[[nodiscard]]
	glm::vec3 worldScale() noexcept;

	void worldPosition(const glm::vec3& worldPos) noexcept;
	void worldRotationQuat(const glm::quat& worldRot) noexcept;
	void worldRotationRadians(const glm::vec3& worldRotRadians) noexcept;
	void worldRotation(const glm::vec3& worldRotDegrees) noexcept;
	void worldScale(const glm::vec3& worldScale) noexcept;

	[[nodiscard]]
	glm::vec3 GetFrontVector() noexcept;
	[[nodiscard]]
	glm::vec3 GetRightVector() noexcept;
	[[nodiscard]]
	glm::vec3 GetUpVector() noexcept;

	// Matrices
	[[nodiscard]]
	glm::mat4 GetMatrix() noexcept;

	[[nodiscard]]
	glm::mat4 GetInverse() noexcept;

	[[nodiscard]]
	glm::mat4 GetWorldMatrix() noexcept;

	void SetAttachedActor(toast::Actor* actor) {
		m_attachedActor = actor;
	}

private:
	// Flags
	bool m_dirtyMatrix = true;
	bool m_dirtyInverse = true;
	bool m_dirtyWorldMatrix = true;
	bool m_dirtyDirectionVectors = true;

	// Local TRS
	alignas(16) glm::vec3 m_position;
	alignas(16) glm::quat m_rotation;
	alignas(16) glm::vec3 m_scale;

	// Editor Euler cache
	alignas(16) glm::vec3 m_eulerDegreesCache { 0.0f, 0.0f, 0.0f };
	bool m_eulerCacheValid = false;

	alignas(16) glm::vec3 m_front;
	alignas(16) glm::vec3 m_right;
	alignas(16) glm::vec3 m_up;

	// Cached matrices
	alignas(16) glm::mat4 m_cachedMatrix;
	alignas(16) glm::mat4 m_cachedInverse;
	alignas(16) glm::mat4 m_cachedWorldMatrix;

	// Cached parent world TRS to avoid repeated traversal when world is clean
	mutable glm::vec3 m_cachedParentWorldPos;
	mutable glm::quat m_cachedParentWorldRot;
	mutable glm::vec3 m_cachedParentWorldScl;

	// Helpers
	void CalcDirectionVectors();
	void UpdateChildrenWorldMatrix();

	// Accumulate parent's world TRS without instantiating/decomposing matrices
	void ComputeParentWorldTRS(glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScl) const noexcept;

	static glm::vec3 SafeCompDiv(const glm::vec3& a, const glm::vec3& b) noexcept;

	toast::Actor* m_attachedActor = nullptr;
};

}

///@TODO FIX THIS SPDLOG IS NOW A PRIVATE LIBRARY OF TOAST
/*
 // This is so fmt recognizes transforms and you can print them on logs
#pragma region fmt /////////////////////////////////////////////////////////////////////////////////////////////////////

template<>
struct fmt::formatter[toast::TransformComponent] {
  constexpr auto parse(format_parse_context& ctx) {
    return ctx.begin();
  }

  template<typename FormatContext>
  auto format(const toast::TransformComponent& t, FormatContext& ctx) {
    return fmt::format_to(
        ctx.out(),
        "Pos: ({:.3f}, {:.3f}, {:.3f}), Rot(deg): ({:.3f}, {:.3f}, {:.3f}), Scale: ({:.3f}, {:.3f}, {:.3f})",
        t.position().x,
        t.position().y,
        t.position().z,
        t.rotation().x,
        t.rotation().y,
        t.rotation().z,
        t.scale().x,
        t.scale().y,
        t.scale().z
    );
  }
};

#pragma endregion
*/
