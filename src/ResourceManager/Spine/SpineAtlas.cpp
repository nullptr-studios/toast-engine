/// @file SpineAtlas.cpp
/// @author dario
/// @date 23/10/2025.

#include "Toast/Resources/Spine/SpineAtlas.hpp"

#include "SpineTextureLoader.hpp"
#include "Toast/Resources/ResourceManager.hpp"

SpineAtlas::~SpineAtlas() {
	delete m_atlas;
}

void SpineAtlas::Load() {
	SetResourceState(resource::ResourceState::LOADING);

	std::vector<uint8_t> buffer {};
	resource::ResourceManager::GetInstance()->OpenFile(m_path, buffer);
	// remove file and just leave dir in stringç
	m_path = m_path.substr(0, m_path.find_last_of('/') + 1);

	m_atlas = new spine::Atlas(
	    reinterpret_cast<const char*>(buffer.data()), static_cast<int>(buffer.size()), m_path.c_str(), SpineTextureLoader::getInstance()
	);

	SetResourceState(resource::ResourceState::LOADEDCPU);
}

spine::Atlas* SpineAtlas::GetAtlasData() const {
	return m_atlas;
}
