/// @file IPostProcess.hpp
/// @author dario
/// @date 23/03/2026.

#pragma once

#include "Framebuffer.hpp"

struct IPostProcess {
	virtual ~IPostProcess() = default;

	virtual void Execute(Framebuffer* inputFBO, Framebuffer* outputFBO) = 0;

#ifdef TOAST_EDITOR
	virtual void Inspector() = 0;
#endif
};
