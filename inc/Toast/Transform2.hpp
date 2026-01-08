/// @file Transform2.hpp
/// @author Xein
/// @date 28 Dec 2025

#pragma once
#include "Toast/Components/TransformComponent.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace toast {

class Actor;

class TransformImpl {
	friend struct Transform2;

public:
	auto position() const noexcept -> glm::vec3;           // NOLINT
	auto position() noexcept -> glm::vec3&;                // NOLINT
	auto rotation() const noexcept -> glm::quat;           // NOLINT
	auto rotation() noexcept -> glm::quat&;                // NOLINT
	auto scale() const noexcept -> glm::vec3;              // NOLINT
	auto scale() noexcept -> glm::vec3&;                   // NOLINT

	auto rotationRadians() const noexcept -> glm::vec3;    // NOLINT
	auto rotationDegrees() const noexcept -> glm::vec3;    // NOLINT

	[[nodiscard]]
	auto matrix() const noexcept -> glm::mat4;

	[[nodiscard]]
	auto inverse() const noexcept -> glm::mat4;

private:
	TransformImpl() = default;

	struct M {
		mutable bool dirtyMatrix = true;
		mutable bool dirtyInverse = true;

		alignas(16) glm::vec3 position;
		alignas(16) glm::quat rotation;
		alignas(16) glm::vec3 scale;

		mutable glm::mat4 cachedMatrix;
		mutable glm::mat4 cachedInverse;
	} m;
};

struct Transform2 {
	// get local by just doing transform.position()
	auto position() const noexcept -> glm::vec3;           // NOLINT
	auto position() noexcept -> glm::vec3&;                // NOLINT
	auto rotation() const noexcept -> glm::quat;           // NOLINT
	auto rotation() noexcept -> glm::quat&;                // NOLINT
	auto scale() const noexcept -> glm::vec3;              // NOLINT
	auto scale() noexcept -> glm::vec3&;                   // NOLINT

	auto rotationRadians() const noexcept -> glm::vec3;    // NOLINT
	auto rotationDegrees() const noexcept -> glm::vec3;    // NOLINT

	TransformImpl local;
	TransformImpl world;

	void UpdateWorldTransform(Actor* parent);

	void FromTransform(const toast::TransformComponent* t);
	void ToTransform(toast::TransformComponent* t) const;
};

}

#pragma region fmt

template<>
struct std::formatter<toast::Transform2> {
	constexpr auto parse(std::format_parse_context& ctx) {
		return ctx.begin();
	}

	auto format(const toast::Transform2& t, std::format_context& ctx) const {
		return std::format_to(
		    ctx.out(),
		    "Transform(\n\tpos: ({:.2f}, {:.2f}, {:.2f}),\n\trot: ({:.2f}, {:.2f}, {:.2f}),\n\tscl: ({:.2f}, {:.2f}, {:.2f})\n)",
		    t.position().x,
		    t.position().y,
		    t.position().z,
		    t.rotationDegrees().x,
		    t.rotationDegrees().y,
		    t.rotationDegrees().z,
		    t.scale().x,
		    t.scale().y,
		    t.scale().z
		);
	}
};

template<>
struct std::formatter<toast::TransformImpl> {
	constexpr auto parse(std::format_parse_context& ctx) {
		return ctx.begin();
	}

	auto format(const toast::TransformImpl& t, std::format_context& ctx) const {
		return std::format_to(
		    ctx.out(),
		    "Transform(\n\tpos: ({:.2f}, {:.2f}, {:.2f}),\n\trot: ({:.2f}, {:.2f}, {:.2f}),\n\tscl: ({:.2f}, {:.2f}, {:.2f})\n)",
		    t.position().x,
		    t.position().y,
		    t.position().z,
		    t.rotationDegrees().x,
		    t.rotationDegrees().y,
		    t.rotationDegrees().z,
		    t.scale().x,
		    t.scale().y,
		    t.scale().z
		);
	}
};

#pragma endregion
