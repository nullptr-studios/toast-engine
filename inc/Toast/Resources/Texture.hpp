/// @file Texture.hpp
/// @author dario
/// @date 17/09/2025

#pragma once

#include "IResource.hpp"

#include <string>

// To create a texture please call ResourceManager::LoadTexture(path)
class Texture : public IResource {
public:
	Texture(std::string path) : IResource(std::move(path), resource::ResourceType::TEXTURE, true) { }

	// non-copyable
	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;

	~Texture() override;

	void Bind(unsigned int slot = 0) const;
	void Unbind(unsigned int slot = 0) const;

	void TextureFiltering(bool linear) const;
	void TextureWrap(bool repeat) const;

	[[nodiscard]]
	unsigned int width() const {
		return m_width;
	}

	[[nodiscard]]
	unsigned int height() const {
		return m_height;
	}

	[[nodiscard]]
	unsigned int id() const {
		return m_textureId;
	}

	static void FlipVertically(bool flip);
	

	void Load() override;
	void LoadMainThread() override;

private:
	
	void LoadPlaceholderTexture();
	
	void CreateOpenGLTexture();

	int m_width = 0;
	int m_height = 0;
	int m_channels = 0; // number of channels loaded from file
	unsigned char* m_pixels = nullptr; // stbi allocated buffer
	unsigned int m_textureId = 0;
};
