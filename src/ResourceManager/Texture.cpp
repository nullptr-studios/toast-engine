/// @file Texture.cpp
/// @author dario
/// @date 20/09/2025.

#include <Toast/Log.hpp>
#include <Toast/Profiler.hpp>
#include <Toast/Renderer/IRendererBase.hpp>
#include <Toast/Resources/ResourceManager.hpp>
#include <Toast/Resources/Texture.hpp>
#include <algorithm>
#include <glad/gl.h>
#include <stb_image.h>
#include <string_view>

namespace {

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif

#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

constexpr float kDefaultAnisotropyLevel = 8.0f;

float GetConfiguredAnisotropyLevel() {
	if (auto* rendererInstance = renderer::IRendererBase::GetInstance()) {
		return std::max(1.0f, rendererInstance->GetRendererConfig().anisotropyLevel);
	}
	return kDefaultAnisotropyLevel;
}

bool SupportsAnisotropicFiltering() {
	static int cachedSupport = -1;
	if (cachedSupport != -1) {
		return cachedSupport == 1;
	}

	GLint extensionCount = 0;
	glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
	for (GLint i = 0; i < extensionCount; ++i) {
		const char* extension = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
		if (extension && std::string_view(extension) == "GL_EXT_texture_filter_anisotropic") {
			cachedSupport = 1;
			return true;
		}
	}

	cachedSupport = 0;
	return false;
}

void ApplyAnisotropy2D() {
	if (!SupportsAnisotropicFiltering()) {
		return;
	}

	const float requestedLevel = GetConfiguredAnisotropyLevel();
	GLfloat maxAnisotropy = 1.0f;
	glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy);
	const GLfloat anisotropy = std::clamp(static_cast<GLfloat>(requestedLevel), 1.0f, maxAnisotropy);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, anisotropy);
}

}    // namespace

Texture::~Texture() {
	// m_pixels is a std::vector, it cleans itself up automatically

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
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
}

void Texture::TextureWrap(bool repeat) const {
	Bind();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
}

void Texture::FlipVertically(bool flip) {
	stbi_set_flip_vertically_on_load_thread(flip);
}

void Texture::Load() {
	PROFILE_ZONE;
	SetResourceState(resource::ResourceState::LOADING);

	std::vector<uint8_t> f {};
	if (!resource::Open(m_path, f)) {
		TOAST_ERROR("Failed to load texture: {0}", m_path);
		SetResourceState(resource::ResourceState::FAILED);
		return;
	}

	int channels = 0;

	stbi_set_flip_vertically_on_load_thread(1);

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

	// Copy pixel data into our owned buffer, then free stbi's allocation immediately
	const size_t dataSize = static_cast<size_t>(m_width) * m_height * channels;
	m_pixels.assign(pixels, pixels + dataSize);
	stbi_image_free(pixels);

	SetResourceState(resource::ResourceState::LOADEDCPU);
}

// Load OpenGl on the main thread
void Texture::LoadMainThread() {
	const auto state = GetResourceState();
	if (state == resource::ResourceState::LOADEDCPU) {
		CreateOpenGLTexture();
	} else if (state == resource::ResourceState::FAILED) {
		LoadPlaceholderTexture();
	} else {
		TOAST_WARN("Texture::LoadMainThread() called in unexpected state ({}) for: {}", static_cast<int>(state), m_path);
		if (state == resource::ResourceState::UNLOADED || state == resource::ResourceState::LOADING) {
			LoadPlaceholderTexture();
		}
	}
}

void Texture::LoadPlaceholderTexture() {
	// generate a raw array of pixeles and upload directly to opengl
	// generate a checkerdoabr purple and black pattern 32x32
	constexpr int size = 32;
	constexpr int channels = 4;
	unsigned char pixels[size * size * channels];
	for (int y = 0; y < size; ++y) {
		for (int x = 0; x < size; ++x) {
			int index = (y * size + x) * channels;
			if (((x / 4) + (y / 4)) % 2 == 0) {
				// purple
				pixels[index + 0] = 255;    // R
				pixels[index + 1] = 0;      // G
				pixels[index + 2] = 255;    // B
				pixels[index + 3] = 255;    // A
			} else {
				// black
				pixels[index + 0] = 0;      // R
				pixels[index + 1] = 0;      // G
				pixels[index + 2] = 0;      // B
				pixels[index + 3] = 255;    // A
			}
		}
	}
	m_width = size;
	m_height = size;
	m_channels = channels;

	// Create OpenGL texture
	glActiveTexture(GL_TEXTURE0);
	glGenTextures(1, const_cast<GLuint*>(&m_textureId));
	glBindTexture(GL_TEXTURE_2D, m_textureId);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);    // reset; Ultralight HUD driver may leave this non-zero

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, reinterpret_cast<void*>(pixels));

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glGenerateMipmap(GL_TEXTURE_2D);
	ApplyAnisotropy2D();

	glBindTexture(GL_TEXTURE_2D, 0);

	// No CPU pixel data to free since we generated it here
}

void Texture::CreateOpenGLTexture() {
	PROFILE_ZONE;
	if (m_textureId) {
		TOAST_ERROR("Tried to generate a texture but it was already generated!?!?!?");
		return;
	}

	SetResourceState(resource::ResourceState::UPLOADING);

	if (m_pixels.empty()) {
		SetResourceState(resource::ResourceState::FAILED);
		TOAST_ERROR("Trying to create OpenGL texture but no pixel data is available!?!");
		LoadPlaceholderTexture();
		return;
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
			m_pixels.clear();
			m_pixels.shrink_to_fit();
			LoadPlaceholderTexture();
			return;
		}
	}

	glActiveTexture(GL_TEXTURE0);
	glGenTextures(1, const_cast<GLuint*>(&m_textureId));
	glBindTexture(GL_TEXTURE_2D, m_textureId);

	// Ensure tight packing for arbitrary widths
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);    // reset; Ultralight HUD driver may leave this non-zero

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, m_width, m_height, 0, format, GL_UNSIGNED_BYTE, m_pixels.data());
	glGenerateMipmap(GL_TEXTURE_2D);
	ApplyAnisotropy2D();

	glBindTexture(GL_TEXTURE_2D, 0);

	// Release CPU pixel data now that it's on the GPU
	m_pixels.clear();
	m_pixels.shrink_to_fit();

	SetResourceState(resource::ResourceState::UPLOADEDGPU);
}
