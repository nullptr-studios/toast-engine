/// @file AtlasSpriteComponent.hpp
/// @author dario
/// @date 04/02/2026.

#pragma once
#include "Toast/Components/TransformComponent.hpp"
#include "spine/Atlas.h"

#include <string>

namespace toast {

/// @class AtlasSpriteComponent
/// @brief Individual sprite instance from an atlas, child of AtlasRendererComponent
/// Each sprite has its own transform and can be manipulated independently
class AtlasSpriteComponent : public TransformComponent {
public:
	REGISTER_TYPE(AtlasSpriteComponent);

	void Init() override;

	void Load(json_t j, bool force_create = true) override;
	json_t Save() const override;

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

	// Getters/Setters
	void SetRegionName(const std::string& regionName) {
		m_regionName = regionName;
	}

	[[nodiscard]]
	const std::string& GetRegionName() const noexcept {
		return m_regionName;
	}

	void SetRegion(spine::AtlasRegion* region) {
		m_region = region;
	}

	[[nodiscard]]
	spine::AtlasRegion* GetRegion() const noexcept {
		return m_region;
	}

	void SetColor(const glm::vec4& color) {
		m_color = color;
	}

	[[nodiscard]]
	const glm::vec4& GetColor() const noexcept {
		return m_color;
	}

	[[nodiscard]]
	uint32_t GetColorABGR() const noexcept {
		// Convert RGBA to ABGR format
		return (static_cast<uint32_t>(m_color.a * 255.0f) << 24) |
		       (static_cast<uint32_t>(m_color.b * 255.0f) << 16) |
		       (static_cast<uint32_t>(m_color.g * 255.0f) << 8) |
		       static_cast<uint32_t>(m_color.r * 255.0f);
	}

private:
	std::string m_regionName;
	spine::AtlasRegion* m_region = nullptr;
	glm::vec4 m_color { 1.0f, 1.0f, 1.0f, 1.0f };  // RGBA
};

}
