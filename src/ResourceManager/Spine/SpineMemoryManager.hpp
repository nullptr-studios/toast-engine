#pragma once

#include "Toast/Resources/ResourceManager.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <spine/Extension.h>
#include <spine/SpineString.h>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>

// Spine uses its own named memory pool to avoid conflicts with the main allocator's re-entrancy guard
// This ensures Spine allocations are always properly tracked without interference
#define SPINE_TRACY_ALLOC(ptr, size) TracyAllocN(ptr, size, "Spine")
#define SPINE_TRACY_FREE(ptr) TracyFreeN(ptr, "Spine")

#else
#define SPINE_TRACY_ALLOC(ptr, size) ((void)0)
#define SPINE_TRACY_FREE(ptr) ((void)0)
#endif

namespace spine {

class EngineSpineExtension : public SpineExtension {
public:
	EngineSpineExtension() {
		TOAST_TRACE("Created EngineSpineExtension");
	}

	void* _alloc(size_t size, const char* file, int line) override {
		(void)file;
		(void)line;
		auto ptr = std::malloc(size);
		SPINE_TRACY_ALLOC(ptr, size);
		return ptr;
	}

	void* _calloc(size_t size, const char* file, int line) override {
		(void)file;
		(void)line;
		auto ptr = std::calloc(size, 1);
		SPINE_TRACY_ALLOC(ptr, size);
		return ptr;
	}

	void* _realloc(void* oldPtr, size_t size, const char* file, int line) override {
		(void)file;
		(void)line;
		// For Tracy: we need to free the old allocation before realloc
		// and register the new allocation after (regardless of whether pointer moved)
		if (oldPtr) {
			SPINE_TRACY_FREE(oldPtr);
		}
		void* newPtr = std::realloc(oldPtr, size);
		if (newPtr) {
			SPINE_TRACY_ALLOC(newPtr, size);
		}
		return newPtr;
	}

	void _free(void* mem, const char* file, int line) override {
		(void)file;
		(void)line;
		if (mem) {
			SPINE_TRACY_FREE(mem);
		}
		std::free(mem);
	}

	char* _readFile(const String& path, int* length) override {
		// Basic file loader (UTF-8 safe, binary mode)
		std::vector<uint8_t> data;
		resource::Open(path.buffer(), data);
		if (length) {
			*length = static_cast<int>(data.size());
		}
		return reinterpret_cast<char*>(data.data());
	}
};

SpineExtension* getDefaultExtension();

}    // namespace spine
