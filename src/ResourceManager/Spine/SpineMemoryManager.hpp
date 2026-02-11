#include "Toast/Resources/ResourceManager.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <spine/Extension.h>
#include <spine/SpineString.h>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
// Use the same re-entrancy guard as the main memory manager
extern thread_local bool g_inTracyCall;

struct SpineTracyGuard {
	bool wasInCall;
	SpineTracyGuard() : wasInCall(g_inTracyCall) { g_inTracyCall = true; }
	~SpineTracyGuard() { g_inTracyCall = wasInCall; }
};

#define SPINE_TRACY_ALLOC(ptr, size) \
	do { \
		if (!g_inTracyCall) { \
			SpineTracyGuard guard; \
			TracyAlloc(ptr, size); \
		} \
	} while (0)

#define SPINE_TRACY_FREE(ptr) \
	do { \
		if (!g_inTracyCall) { \
			SpineTracyGuard guard; \
			TracyFree(ptr); \
		} \
	} while (0)
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

	void* _realloc(void* ptr, size_t size, const char* file, int line) override {
		(void)file;
		(void)line;
		return std::realloc(ptr, size);
	}

	void _free(void* mem, const char* file, int line) override {
		(void)file;
		(void)line;
		SPINE_TRACY_FREE(mem);
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
