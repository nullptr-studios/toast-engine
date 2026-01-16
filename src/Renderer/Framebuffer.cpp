/// @file Framebuffer.cpp
/// @author dario
/// @date 11/10/2025.

#include "Toast/Renderer/Framebuffer.hpp"

#include "Toast/Log.hpp"

#include <glad/gl.h>

Framebuffer::Framebuffer(const Specs& specs) : m_specs(specs) {
	glGenFramebuffers(1, &m_fbo);
	if (m_fbo == 0) {
		throw ToastException("Failed to generate framebuffer");
	}
}

Framebuffer::Framebuffer(Framebuffer&& o) noexcept {
	steal(o);
}

Framebuffer& Framebuffer::operator=(Framebuffer&& o) noexcept {
	if (this != &o) {
		destroy();
		steal(o);
	}
	return *this;
}

void Framebuffer::Build() {
	if (m_specs.width <= 0 || m_specs.height <= 0) {
		throw ToastException("Framebuffer dimensions must be > 0");
	}

	bind();

	// Clean up previous attachments
	for (GLuint t : m_colorTextures) {
		if (t) {
			glDeleteTextures(1, &t);
		}
	}
	m_colorTextures.clear();

	if (m_depthTexture) {
		glDeleteTextures(1, &m_depthTexture);
		m_depthTexture = 0;
	}
	if (m_depthRbo) {
		glDeleteRenderbuffers(1, &m_depthRbo);
		m_depthRbo = 0;
	}

	// Create color attachments
	GLenum texTarget = m_specs.multisample ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
	m_colorTextures.resize(m_colorSpecs.size(), 0);

	for (size_t i = 0; i < m_colorSpecs.size(); ++i) {
		GLuint tex = 0;
		glGenTextures(1, &tex);
		if (!tex) {
			throw ToastException("Failed to create color texture for FBO");
		}

		glBindTexture(texTarget, tex);
		if (m_specs.multisample) {
			glTexImage2DMultisample(texTarget, m_specs.samples, m_colorSpecs[i].internalFormat, m_specs.width, m_specs.height, GL_TRUE);
		} else {
			glTexImage2D(
			    texTarget, 0, m_colorSpecs[i].internalFormat, m_specs.width, m_specs.height, 0, m_colorSpecs[i].format, m_colorSpecs[i].type, nullptr
			);

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		}

		// attach
		glFramebufferTexture2D(GL_FRAMEBUFFER, static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i), texTarget, tex, 0);

		m_colorTextures[i] = tex;
	}

	// Depth attachment
	if (m_depthMode == DepthMode::Renderbuffer) {
		glGenRenderbuffers(1, &m_depthRbo);
		glBindRenderbuffer(GL_RENDERBUFFER, m_depthRbo);
		if (m_specs.multisample) {
			glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_specs.samples, m_depthInternalFormat, m_specs.width, m_specs.height);
		} else {
			glRenderbufferStorage(GL_RENDERBUFFER, m_depthInternalFormat, m_specs.width, m_specs.height);
		}
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depthRbo);
		// unbind rbo
		glBindRenderbuffer(GL_RENDERBUFFER, 0);
	} else if (m_depthMode == DepthMode::Texture) {
		GLuint tex = 0;
		glGenTextures(1, &tex);
		GLenum texTargetDepth = m_specs.multisample ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
		glBindTexture(texTargetDepth, tex);
		if (m_specs.multisample) {
			glTexImage2DMultisample(texTargetDepth, m_specs.samples, m_depthInternalFormat, m_specs.width, m_specs.height, GL_TRUE);
		} else {
			GLenum format = (m_depthInternalFormat == GL_DEPTH24_STENCIL8) ? GL_DEPTH_STENCIL : GL_DEPTH_COMPONENT;
			GLenum type = (m_depthInternalFormat == GL_DEPTH24_STENCIL8) ? GL_UNSIGNED_INT_24_8 : GL_FLOAT;
			glTexImage2D(texTargetDepth, 0, m_depthInternalFormat, m_specs.width, m_specs.height, 0, format, type, nullptr);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		}

		// attach as depth or depth-stencil depending on format
		if (m_depthInternalFormat == GL_DEPTH24_STENCIL8) {
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, texTargetDepth, tex, 0);
		} else {
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, texTargetDepth, tex, 0);
		}

		m_depthTexture = tex;
		glBindTexture(texTargetDepth, 0);
	}

	if (!m_colorSpecs.empty()) {
		std::vector<GLenum> draws;
		draws.reserve(m_colorSpecs.size());
		for (size_t i = 0; i < m_colorSpecs.size(); ++i) {
			draws.push_back(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i));
		}
		glDrawBuffers(static_cast<GLsizei>(draws.size()), draws.data());
	} else {
		// no color attachments -> disable draw buffer
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
	}

	// Check complete
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		std::string msg = "Framebuffer is not complete: 0x" + intToHex(status);
		unbind();
		throw ToastException(msg);
	}

	// restore binding
	unbind();
}

void Framebuffer::BlitTo(const Framebuffer* target, GLbitfield mask, GLenum filter, unsigned srcAttachment, unsigned dstAttachment) const {
	// Preserve previous framebuffer bindings and buffer enums
	GLint prevReadFbo = 0, prevDrawFbo = 0;
	GLint prevReadBuf = 0, prevDrawBuf = 0;
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
	glGetIntegerv(GL_READ_BUFFER, &prevReadBuf);
	glGetIntegerv(GL_DRAW_BUFFER, &prevDrawBuf);

	// bind read and draw
	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target ? target->m_fbo : 0);

	// If we're blitting color bits, select appropriate color attachments
	if (mask & GL_COLOR_BUFFER_BIT) {
		// validate indices
		if (srcAttachment >= m_colorTextures.size()) {
			TOAST_WARN("BlitTo: srcAttachment {0} out of range, clamping to 0", srcAttachment);
			srcAttachment = 0;
		}
		if (target && dstAttachment >= target->m_colorTextures.size()) {
			TOAST_WARN("BlitTo: dstAttachment {0} out of range for target, clamping to 0", dstAttachment);
			dstAttachment = 0;
		}

		glReadBuffer(static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + srcAttachment));
		if (target) {
			glDrawBuffer(static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + dstAttachment));
		} else {
			// if target is null (default framebuffer) map to back buffer
			glDrawBuffer(GL_BACK);
		}
	}

	glBlitFramebuffer(
	    0,
	    0,
	    m_specs.width,
	    m_specs.height,
	    0,
	    0,
	    target ? target->m_specs.width : m_specs.width,
	    target ? target->m_specs.height : m_specs.height,
	    mask,
	    filter
	);

	// restore previous read/draw buffers and framebuffer bindings
	if (mask & GL_COLOR_BUFFER_BIT) {
		glReadBuffer(static_cast<GLenum>(prevReadBuf));
		glDrawBuffer(static_cast<GLenum>(prevDrawBuf));
	}

	glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFbo);
}

uint32_t Framebuffer::ReadPixel(unsigned colorAttachmentIndex, int x, int y) const {
	if (colorAttachmentIndex >= m_colorTextures.size()) {
		throw ToastException("colorAttachmentIndex out of range");
	}
	if (m_specs.multisample) {
		throw ToastException("ReadPixel: framebuffer is multisampled; blit to resolved FBO first");
	}

	GLint prev = 0;
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo);
	glReadBuffer(static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + colorAttachmentIndex));

	unsigned char pixel[4] = { 0, 0, 0, 0 };
	glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);

	// restore
	glBindFramebuffer(GL_READ_FRAMEBUFFER, prev);

	uint32_t out = static_cast<uint32_t>(pixel[0]) | (static_cast<uint32_t>(pixel[1]) << 8) | (static_cast<uint32_t>(pixel[2]) << 16) |
	               (static_cast<uint32_t>(pixel[3]) << 24);
	return out;
}

void Framebuffer::destroy() {
	if (m_colorTextures.size()) {
		for (GLuint t : m_colorTextures) {
			if (t) {
				glDeleteTextures(1, &t);
			}
		}
		m_colorTextures.clear();
	}
	if (m_depthTexture) {
		glDeleteTextures(1, &m_depthTexture);
		m_depthTexture = 0;
	}
	if (m_depthRbo) {
		glDeleteRenderbuffers(1, &m_depthRbo);
		m_depthRbo = 0;
	}
	if (m_fbo) {
		glDeleteFramebuffers(1, &m_fbo);
		m_fbo = 0;
	}
}

void Framebuffer::steal(Framebuffer& o) noexcept {
	m_specs = o.m_specs;
	m_fbo = o.m_fbo;
	m_colorSpecs = std::move(o.m_colorSpecs);
	m_colorTextures = std::move(o.m_colorTextures);
	m_depthTexture = o.m_depthTexture;
	m_depthRbo = o.m_depthRbo;
	m_depthMode = o.m_depthMode;
	m_depthInternalFormat = o.m_depthInternalFormat;

	// null out source
	o.m_fbo = 0;
	o.m_colorTextures.clear();
	o.m_depthTexture = 0;
	o.m_depthRbo = 0;
	o.m_colorSpecs.clear();
}
