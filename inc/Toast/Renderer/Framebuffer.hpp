/// @file Framebuffer.hpp
/// @author dario
/// @date 11/10/2025.

#pragma once
#include <glad/gl.h>
#include <iostream>
#include <string>
#include <vector>

class Framebuffer {
public:
	enum class DepthMode : uint8_t {
		None,
		Renderbuffer,
		Texture
	};

	struct Specs {
		int width = 800;
		int height = 600;
		bool multisample = false;
		int samples = 4;
	};

	// Create framebuffer with given specs; attachments are created by helper calls or Build()
	explicit Framebuffer(const Specs& specs);

	// non-copyable
	Framebuffer(const Framebuffer&) = delete;
	Framebuffer& operator=(const Framebuffer&) = delete;

	// moveable
	Framebuffer(Framebuffer&& o) noexcept;
	Framebuffer& operator=(Framebuffer&& o) noexcept;

	~Framebuffer() {
		destroy();
	}

	// Add a 2D color attachment to be created when Build() is called.
	void AddColorAttachment(GLenum internalFormat = GL_RGBA8, GLenum format = GL_RGBA, GLenum type = GL_UNSIGNED_BYTE) {
		m_colorSpecs.push_back({ internalFormat, format, type });
	}

	// Add depth attachment.
	void AddDepthAttachment(bool asTexture = false, GLenum internalFormat = GL_DEPTH32F_STENCIL8) {
		m_depthMode = asTexture ? DepthMode::Texture : DepthMode::Renderbuffer;
		m_depthInternalFormat = internalFormat;
	}

	// Build/create all attachments and complete the framebuffer.
	// Call after adding attachments or after resizing.
	void Build();

	// Bind this framebuffer (GL_FRAMEBUFFER)
	void bind() const {
		glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
	}

	// Unbind to default framebuffer (bind 0)
	static void unbind() {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	// Resize and rebuild attachments
	void Resize(int w, int h) {
		if (w <= 0 || h <= 0) {
			return;
		}
		m_specs.width = w;
		m_specs.height = h;
		Build();
	}

	// Blit this framebuffer to target framebuffer Mask can be GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT
	// srcAttachment / dstAttachment select which color attachment index to blit (default 0)
	void BlitTo(
	    const Framebuffer* target, GLbitfield mask = GL_COLOR_BUFFER_BIT, GLenum filter = GL_NEAREST, unsigned srcAttachment = 0,
	    unsigned dstAttachment = 0
	) const;

	// Read pixel from a given color attachment (index), returned as a 32-bit RGBA (unsigned byte) value.
	uint32_t ReadPixel(unsigned colorAttachmentIndex, int x, int y) const;

	// Get OpenGL texture id of color attachment or 0 if not present
	// For multisampled textures the returned texture is GL_TEXTURE_2D_MULTISAMPLE target texture id
	GLuint GetColorTexture(unsigned idx) const {
		if (idx >= m_colorTextures.size()) {
			return 0;
		}
		return m_colorTextures[idx];
	}

	// Get depth texture id if depth was created as texture
	GLuint GetDepthTexture() const {
		return m_depthTexture;
	}

	// Getters
	int Width() const {
		return m_specs.width;
	}

	int Height() const {
		return m_specs.height;
	}

	bool IsMultisample() const {
		return m_specs.multisample;
	}

	GLuint Handle() const {
		return m_fbo;
	}

private:
	struct ColorSpec {
		GLenum internalFormat;
		GLenum format;
		GLenum type;
	};

	Specs m_specs;
	GLuint m_fbo = 0;
	std::vector<ColorSpec> m_colorSpecs;
	std::vector<GLuint> m_colorTextures;    // ogl texture ids for color attachments
	GLuint m_depthTexture = 0;
	GLuint m_depthRbo = 0;
	DepthMode m_depthMode = DepthMode::None;
	GLenum m_depthInternalFormat = GL_DEPTH24_STENCIL8;

	void destroy();

	// helper to transfer ownership when moving
	void steal(Framebuffer& o) noexcept;

	static std::string intToHex(GLenum e) {
		char buf[16];
		sprintf(buf, "%X", static_cast<unsigned>(e));
		return std::string(buf);
	}
};
