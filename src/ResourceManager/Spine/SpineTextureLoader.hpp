/// @file SpineTextureLoader.hpp
/// @author dario
/// @date 23/10/2025.

#pragma once
#include "Toast/Log.hpp"
#include "spine/Atlas.h"
#include "spine/TextureLoader.h"

class SpineTextureLoader : public spine::TextureLoader {
public:
	SpineTextureLoader() {
		TOAST_TRACE("Created SpineTextureLoader");
	}

	void load(spine::AtlasPage& page, const spine::String& path) override;
	void unload(void* texture) override;

	static SpineTextureLoader* getInstance() {
		return &instance;
	}

public:
	static SpineTextureLoader instance;
};
