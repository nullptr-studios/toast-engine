/// @file IPostProcess.hpp
/// @author dario
/// @date 23/03/2026.

#pragma once


struct IPostProcess {
	virtual void Execute(unsigned int inTex, unsigned int outFbo) = 0;
};