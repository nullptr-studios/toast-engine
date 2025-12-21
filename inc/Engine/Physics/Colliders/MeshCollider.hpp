/**
 * @file MeshCollider.hpp
 * @author Iñaki
 * @date 09/10/2025
 *
 * @brief [TODO: Brief description of the files purpose]
 */
#pragma once
#include "Collider.hpp"

namespace physics {
/// @brief Type of collider that will be used by physics
class MeshCollider : public ICollider {
public:
	// REGISTER_TYPE(MeshCollider); // FIXME: YOU SHOULD NOT REGISTER COMPONENTS THAT FUCKING CRASH THE EDITOR WHEN USED

	[[nodiscard]]
	constexpr ColliderType collider_type() const override {
		return ColliderType::Mesh;
	}

	/// @brief Initializes the collider to be a mesh
	void Init() override;

	/// @brief Destroys the collider
	void Destroy() override;
};
}
