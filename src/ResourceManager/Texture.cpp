/// @file Texture.cpp
/// @author dario
/// @date 20/09/2025.

#include "Toast/Resources/Texture.hpp"

#include "Toast/Log.hpp"
#include "Toast/Profiler.hpp"
#include "Toast/Resources/ResourceManager.hpp"

#include <glad/glad.h>
#include <stb/stb_image.h>

Texture::~Texture() {
	if (m_pixels) {
		stbi_image_free(m_pixels);    // stbi allocated
		m_pixels = nullptr;
	}

	if (m_textureId) {
		glBindTexture(GL_TEXTURE_2D, m_textureId);                  // bind
		glDeleteTextures(1, static_cast<GLuint*>(&m_textureId));    // delete
		glBindTexture(GL_TEXTURE_2D, 0);                            // unbind
	}
}

void Texture::Bind(unsigned int slot) const {
	glActiveTexture(GL_TEXTURE0 + slot);
	glBindTexture(GL_TEXTURE_2D, m_textureId);
}

void Texture::Unbind(unsigned int slot) const {
	if (m_textureId) {
		glActiveTexture(GL_TEXTURE0 + slot);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
}

void Texture::TextureFiltering(bool linear) const {
	Bind();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
}

void Texture::TextureWrap(bool repeat) const {
	Bind();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
}

void Texture::FlipVertically(bool flip) {
	stbi_set_flip_vertically_on_load(flip);
}

void Texture::Load() {
	PROFILE_ZONE;
	SetResourceState(resource::ResourceState::LOADING);

	std::vector<uint8_t> f {};
	if (!resource::ResourceManager::GetInstance()->OpenFile(m_path, f)) {
		TOAST_ERROR("Failed to load texture: {0}", m_path);
		SetResourceState(resource::ResourceState::FAILED);
		return;
	}

	int channels = 0;

	// Decode without forcing 4 channels
	unsigned char* pixels = stbi_load_from_memory(f.data(), static_cast<int>(f.size()), &m_width, &m_height, &channels, 0);
	if (!pixels) {
		TOAST_ERROR("Failed to load texture from memory: {0}", m_path);
		SetResourceState(resource::ResourceState::FAILED);
		return;
	}

	if (m_width <= 0 || m_height <= 0 || channels <= 0) {
		TOAST_ERROR("Invalid texture dimensions or channels: {0}", m_path);
		stbi_image_free(pixels);
		SetResourceState(resource::ResourceState::FAILED);
		return;
	}

	m_channels = channels;
	m_pixels = pixels;    // Take ownership

	SetResourceState(resource::ResourceState::LOADEDCPU);
}

// Load OpenGl on the main thread
void Texture::LoadMainThread() {
	CreateOpenGLTexture();
}

void Texture::CreateOpenGLTexture(unsigned int slot) {
	PROFILE_ZONE;
	if (m_textureId) {
		TOAST_ERROR("Tried to generate a texture but it was already generated!?!?!?");
		return;
	}

	SetResourceState(resource::ResourceState::UPLOADING);

	if (m_pixels == nullptr) {
		SetResourceState(resource::ResourceState::FAILED);
		throw ToastException("Trying to create OpenGL texture but no pixel data is available!?!");
	}

	// Determine appropriate GL formats
	GLenum format = GL_RGBA;
	GLenum internalFormat = GL_RGBA8;
	switch (m_channels) {
		case 1:
			format = GL_RED;
			internalFormat = GL_R8;
			break;    // grayscale
		case 2:
			format = GL_RG;
			internalFormat = GL_RG8;
			break;    // treat as RG
		case 3:
			format = GL_RGB;
			internalFormat = GL_RGB8;
			break;    // RGB
		case 4:
			format = GL_RGBA;
			internalFormat = GL_RGBA8;
			break;    // RGBA
		default: {
			TOAST_ERROR("Unsupported channel count {0} for texture: {1}", m_channels, m_path);
			SetResourceState(resource::ResourceState::FAILED);
			stbi_image_free(m_pixels);
			m_pixels = nullptr;
			return;
		}
	}

	glActiveTexture(GL_TEXTURE0 + slot);
	glGenTextures(1, const_cast<GLuint*>(&m_textureId));
	glBindTexture(GL_TEXTURE_2D, m_textureId);

	// Ensure tight packing for arbitrary widths
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, m_width, m_height, 0, format, GL_UNSIGNED_BYTE, reinterpret_cast<void*>(m_pixels));

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	glBindTexture(GL_TEXTURE_2D, 0);

	// Release CPU pixel data now that it's on the GPU
	if (m_pixels) {
		stbi_image_free(m_pixels);
		m_pixels = nullptr;
	}

	SetResourceState(resource::ResourceState::UPLOADEDGPU);
}
