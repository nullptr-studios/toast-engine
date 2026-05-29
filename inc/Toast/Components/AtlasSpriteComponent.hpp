/// @file AtlasSpriteComponent.hpp
/// @author dario
/// @date 04/02/2026.

#pragma once
#include "Toast/Components/TransformComponent.hpp"
#include "Toast/Renderer/IRenderable.hpp"
#include "spine/Atlas.h"

#include <string>
#include <string_view>

namespace toast {

/// @class AtlasSpriteComponent
/// @brief Individual sprite instance from an atlas, child of AtlasRendererComponent
/// Each sprite has its own transform and can be manipulated independently
class AtlasSpriteComponent : public TransformComponent {
public:
	REGISTER_TYPE(AtlasSpriteComponent);

	void Init() override;
	void Destroy() override;

	void position(const glm::vec3& position) noexcept;
	void rotation(const glm::vec3& degrees) noexcept;
	void rotationRadians(const glm::vec3& rotation) noexcept;
	void rotationQuat(const glm::quat& quaternion) noexcept;
	void scale(const glm::vec3& scale) noexcept;
	void worldPosition(const glm::vec3& worldPos) noexcept;
	void worldRotationQuat(const glm::quat& worldRot) noexcept;
	void worldRotationRadians(const glm::vec3& worldRotRadians) noexcept;
	void worldRotation(const glm::vec3& worldRotDegrees) noexcept;
	void worldScale(const glm::vec3& worldScale) noexcept;

	using TransformComponent::position;
	using TransformComponent::rotation;
	using TransformComponent::rotationRadians;
	using TransformComponent::rotationQuat;
	using TransformComponent::scale;
	using TransformComponent::worldPosition;
	using TransformComponent::worldRotationQuat;
	using TransformComponent::worldRotationRadians;
	using TransformComponent::worldRotation;
	using TransformComponent::worldScale;

	void Load(json_t j, bool force_create = true) override;
	json_t Save() const override;

	void SetParentDirtyBool(bool* b) {
		m.parentdirty = b;
	}

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

	// Getters/Setters
	void SetRegionName(std::string_view region_name) {
		m.regionName = region_name;
	}

	[[nodiscard]]
	const std::string& GetRegionName() const noexcept {
		return m.regionName;
	}

	void SetRegion(spine::AtlasRegion* region) {
		m.region = region;
	}

	[[nodiscard]]
	spine::AtlasRegion* GetRegion() const noexcept {
		return m.region;
	}

	void SetColor(const glm::vec4& color) {
		m.color = color;
	}

	[[nodiscard]]
	const glm::vec4& GetColor() const noexcept {
		return m.color;
	}

	[[nodiscard]]
	uint32_t GetColorABGR() const noexcept {
		// Convert RGBA to ABGR format
		return (static_cast<uint32_t>(m.color.a * 255.0f) << 24) | (static_cast<uint32_t>(m.color.b * 255.0f) << 16) |
		       (static_cast<uint32_t>(m.color.g * 255.0f) << 8) | static_cast<uint32_t>(m.color.r * 255.0f);
	}

private:
	struct {
		std::string regionName;
		spine::AtlasRegion* region = nullptr;
		glm::vec4 color { 1.0f, 1.0f, 1.0f, 1.0f };    // RGBA
		bool* parentdirty = nullptr;
	} m;
};

}
