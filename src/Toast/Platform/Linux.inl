/// @file   Linux.inl
/// @author Xein
/// @date   13/04/25

#ifndef WIN32
#pragma once
#include <Toast/Engine.hpp>

int main(int argc, char** argv) {
	// Sets the .so path to ./modules
	setenv("LD_LIBRARY_PATH", "./modules", 1);

	toast::Engine* app = toast::CreateApplication();    // NOLINT
	if (!app) {
		return -1;
	}

	app->Run(argc, argv);

	delete app;
	return 0;
}

#endif
