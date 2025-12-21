/// @file   Windows.inl
/// @author Xein
/// @date   13/04/25

#pragma once
#ifdef _WIN32

#include "CrashHandler.hpp"

#include <Engine/Toast/Engine.hpp>

// Export variables to enable high performance graphics on laptops with dual GPUs
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

// clang-format off
#include <windows.h> // NOLINT
#include <shellapi.h> // NOLINT
// clang-format on

// This is the main entrypoint for the application
int main(int argc, char** argv) {
	toast::Engine* app = toast::CreateApplication();    // NOLINT
	if (!app) {
		return -1;
	}

	InstallCrashHandler();

	app->Run(argc, argv);

	delete app;

	return 0;
}

#endif
