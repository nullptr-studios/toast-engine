/// @file SpineSkeletonData.cpp
/// @author dario
/// @date 23/10/2025.

#include "Engine/Resources/Spine/SpineAtlas.hpp"
#include "spine/SkeletonBinary.h"
#include "spine/SkeletonJson.h"

#include <Engine/Resources/ResourceManager.hpp>
#include <Engine/Resources/Spine/SpineSkeletonData.hpp>
#include <Engine/Resources/Texture.hpp>

SpineSkeletonData::~SpineSkeletonData() {
	delete m_skeletonData;
}

void SpineSkeletonData::Load() {
	if (!m_atlas) {
		TOAST_ERROR("SpineSkeletonData::Load() atlas is not set");
	}
	SetResourceState(resource::ResourceState::LOADING);

	std::vector<uint8_t> buffer {};
	resource::ResourceManager::GetInstance()->OpenFile(m_path, buffer);

	if (m_path.ends_with("l")) {
		// .skel file
		spine::SkeletonBinary binary(m_atlas->GetAtlasData());
		binary.setScale(0.02f);
		m_skeletonData = binary.readSkeletonData(buffer.data(), static_cast<int>(buffer.size()));
	} else {
		// .json file
		spine::SkeletonJson json(m_atlas->GetAtlasData());
		json.setScale(0.02f);
		m_skeletonData = json.readSkeletonData(reinterpret_cast<const char*>(buffer.data()));
	}

	if (!m_skeletonData) {
		TOAST_ERROR("SpineSkeletonData::Load() failed: {}", m_path);
		SetResourceState(resource::ResourceState::FAILED);
		return;
	}

	SetResourceState(resource::ResourceState::LOADEDCPU);
}

spine::SkeletonData* SpineSkeletonData::GetSkeletonData() const {
	return m_skeletonData;
}
