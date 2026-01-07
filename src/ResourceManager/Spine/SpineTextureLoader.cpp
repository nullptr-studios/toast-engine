/// @file SpineTextureLoader.cpp
/// @author dario
/// @date 23/10/2025.

#include "SpineTextureLoader.hpp"

#include "Toast/Resources/ResourceManager.hpp"
#include "Toast/Resources/Texture.hpp"
#include "spine/Atlas.h"
#include "spine/SpineString.h"

SpineTextureLoader SpineTextureLoader::instance;

void SpineTextureLoader::load(spine::AtlasPage& page, const spine::String& path) {
	// Texture::FlipVertically(false);
	auto texture = resource::ResourceManager::GetInstance()->LoadResource<Texture>(path.buffer());
	page.texture = new std::shared_ptr<Texture>(texture);
	// Texture::FlipVertically(true);

	page.width = texture->width();
	page.height = texture->height();
}

void SpineTextureLoader::unload(void* texture) {
	if (!texture) {
		return;
	}
	auto texPtr = static_cast<std::shared_ptr<Texture>*>(texture);
	texPtr->reset();
	delete texPtr;
}
