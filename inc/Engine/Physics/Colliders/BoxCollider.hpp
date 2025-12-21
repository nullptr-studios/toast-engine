/**
 * @file BoxCollider.hpp
 * @author Iñaki
 * @date 08/10/2025
 * @brief [TODO: Brief description of the files purpose]
 */

#pragma once
#include "Collider.hpp"
#include "Engine/Input/Action.hpp"
#include "Engine/Toast/Components/TransformComponent.hpp"

namespace physics {
/// @brief Exact collider component that will be used for physics collisions
class BoxCollider final : public ICollider {
public:
	REGISTER_TYPE(BoxCollider);

	[[nodiscard]]
	constexpr ColliderType collider_type() const override {
		return ColliderType::Box;
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

	/// @brief  Setter for the box size
	void SetSize(glm::vec2 size);

	/// @brief Setter for the box position
	void SetPosition(glm::vec2 position);

	/// @brief Setter for the box rotation
	void SetRotation(float rotation);

	/// @brief Getter for the box size
	[[nodiscard]]
	glm::vec2 GetSize(bool local = false) const;

	/// @brief Getter for the box position
	[[nodiscard]]
	glm::vec2 GetPosition(bool local = false) const;

	/// @brief  Getter for the box rotation
	[[nodiscard]]
	float GetRotation(bool local = false) const;

private:
	glm::vec2 m_position = glm::vec2(0, 0);
	glm::vec2 m_size = glm::vec2(10.0f, 10.0f);
	float m_angle = 0.0f;
};
}
