/// @file SpineSkeletonData.hpp
/// @author dario
/// @date 23/10/2025.

#pragma once
#include "SpineAtlas.hpp"
#include "Toast/Resources/IResource.hpp"
#include "spine/SkeletonData.h"

#include <string>

class SpineSkeletonData : public IResource {
public:
	/// @param path Path to the .json or .skel file
	/// @param atlas Shared pointer to the SpineAtlas resource
	SpineSkeletonData(std::string path, std::shared_ptr<SpineAtlas> atlas)
	    : IResource(std::move(path), resource::ResourceType::SPINE_SKELETON_DATA, false),
	      m_skeletonData(nullptr),
	      m_atlas(std::move(atlas)) { }

	~SpineSkeletonData() override;

	void Load() override;

	[[nodiscard]]
	spine::SkeletonData* GetSkeletonData() const;

private:
	std::shared_ptr<SpineAtlas> m_atlas = nullptr;
	spine::SkeletonData* m_skeletonData;
};
