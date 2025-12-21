/**
 * @file CircleCollider.hpp
 * @author Iñaki
 * @date 08/10/2025
 *
 * @brief [TODO: Brief description of the files purpose]
 */
#pragma once
#include "Collider.hpp"
#include "glm/vec2.hpp"

namespace physics {

/// @brief Circle collider used for collisions
class CircleCollider : public ICollider {
public:
	REGISTER_TYPE(CircleCollider);

	[[nodiscard]]
	constexpr ColliderType collider_type() const override {
		return ColliderType::Circle;
	}

	void Begin() override;
	void EditorTick() override;
	void Destroy() override;

	[[nodiscard]]
	json_t Save() const override;
	void Load(json_t j, bool force_create = true) override;
#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

	/// @brief  Setter for the collider position
	void SetPosition(glm::vec2 position);

	/// @brief Setter for the circle radius
	void SetRadius(float radius);

	//// @brief Getter for the circle radius
	[[nodiscard]]
	float GetRadius(bool local = false) const;

	/// @brief Getter for the position
	[[nodiscard]]
	glm::vec2 GetPosition(bool local = false) const;

private:
	float m_radius = 10.0f;
	glm::vec2 m_position = glm::vec2(0.0f);
};

}
