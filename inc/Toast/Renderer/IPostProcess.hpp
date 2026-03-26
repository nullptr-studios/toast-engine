/// @file IPostProcess.hpp
/// @author dario
/// @date 23/03/2026.

#pragma once

#include "Framebuffer.hpp"
#include "Toast/ISerializable.hpp"

struct IPostProcess {
	virtual ~IPostProcess() = default;

	virtual void Execute(Framebuffer* inputFBO, Framebuffer* outputFBO) = 0;
	
	virtual json_t Save() = 0;
	virtual void Load(json_t&) = 0;

#ifdef TOAST_EDITOR
	virtual void Inspector() = 0;
#endif
};
