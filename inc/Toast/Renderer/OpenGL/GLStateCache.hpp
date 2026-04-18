#pragma once

#include <glad/gl.h>
#include <array>

namespace renderer {

namespace detail {

struct GlStateCache {
	bool valid = false;
	bool depthTest = false;
	bool blend = false;
	bool depthMask = true;
	bool cullFace = true;
	bool scissorTest = false;
	bool multisample = false;
	bool sampleAlphaToCoverage = false;
	GLenum depthFunc = GL_LESS;
	GLenum blendSrc = GL_ONE;
	GLenum blendDst = GL_ZERO;
	std::array<GLboolean, 4> colorMask { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
};

inline GlStateCache g_glStateCache {};

}    // namespace detail

inline void InvalidateCachedGlState() {
	detail::g_glStateCache.valid = false;
}

inline void SetDepthTest(bool enabled) {
	if (detail::g_glStateCache.valid && detail::g_glStateCache.depthTest == enabled) {
		return;
	}
	enabled ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
	detail::g_glStateCache.depthTest = enabled;
	detail::g_glStateCache.valid = true;
}

inline void SetBlend(bool enabled) {
	if (detail::g_glStateCache.valid && detail::g_glStateCache.blend == enabled) {
		return;
	}
	enabled ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
	detail::g_glStateCache.blend = enabled;
	detail::g_glStateCache.valid = true;
}

inline void SetDepthMask(bool enabled) {
	if (detail::g_glStateCache.valid && detail::g_glStateCache.depthMask == enabled) {
		return;
	}
	glDepthMask(enabled ? GL_TRUE : GL_FALSE);
	detail::g_glStateCache.depthMask = enabled;
	detail::g_glStateCache.valid = true;
}

inline void SetDepthFunc(GLenum func) {
	if (detail::g_glStateCache.valid && detail::g_glStateCache.depthFunc == func) {
		return;
	}
	glDepthFunc(func);
	detail::g_glStateCache.depthFunc = func;
	detail::g_glStateCache.valid = true;
}

inline void SetBlendFunc(GLenum src, GLenum dst) {
	if (detail::g_glStateCache.valid && detail::g_glStateCache.blendSrc == src && detail::g_glStateCache.blendDst == dst) {
		return;
	}
	glBlendFunc(src, dst);
	detail::g_glStateCache.blendSrc = src;
	detail::g_glStateCache.blendDst = dst;
	detail::g_glStateCache.valid = true;
}

inline void SetColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
	const std::array<GLboolean, 4> next { r, g, b, a };
	if (detail::g_glStateCache.valid && detail::g_glStateCache.colorMask == next) {
		return;
	}
	glColorMask(r, g, b, a);
	detail::g_glStateCache.colorMask = next;
	detail::g_glStateCache.valid = true;
}

inline void SetCullFace(bool enabled) {
	if (detail::g_glStateCache.valid && detail::g_glStateCache.cullFace == enabled) {
		return;
	}
	enabled ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
	detail::g_glStateCache.cullFace = enabled;
	detail::g_glStateCache.valid = true;
}

inline void SetScissorTest(bool enabled) {
	if (detail::g_glStateCache.valid && detail::g_glStateCache.scissorTest == enabled) {
		return;
	}
	enabled ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
	detail::g_glStateCache.scissorTest = enabled;
	detail::g_glStateCache.valid = true;
}

inline void SetMultisample(bool enabled) {
	if (detail::g_glStateCache.valid && detail::g_glStateCache.multisample == enabled) {
		return;
	}
	enabled ? glEnable(GL_MULTISAMPLE) : glDisable(GL_MULTISAMPLE);
	detail::g_glStateCache.multisample = enabled;
	detail::g_glStateCache.valid = true;
}

inline void SetSampleAlphaToCoverage(bool enabled) {
	if (detail::g_glStateCache.valid && detail::g_glStateCache.sampleAlphaToCoverage == enabled) {
		return;
	}
	enabled ? glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE) : glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
	detail::g_glStateCache.sampleAlphaToCoverage = enabled;
	detail::g_glStateCache.valid = true;
}

}    // namespace renderer


