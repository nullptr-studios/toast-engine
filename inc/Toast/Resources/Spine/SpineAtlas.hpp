/// @file SpineAtlas.hpp
/// @author dario
/// @date 23/10/2025.

#pragma once
#include "Toast/Resources/IResource.hpp"
#include "spine/Atlas.h"

#include <string>

class SpineAtlas : public IResource {
public:
	SpineAtlas(std::string path) : IResource(std::move(path), resource::ResourceType::SPINE_ATLAS, false), m_atlas(nullptr) { }

	~SpineAtlas() override;

	void Load() override;

	[[nodiscard]]
	spine::Atlas* GetAtlasData() const;

private:
	spine::Atlas* m_atlas;
};
