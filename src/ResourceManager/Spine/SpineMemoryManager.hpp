#include "Toast/Resources/ResourceManager.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <spine/Extension.h>
#include <spine/SpineString.h>

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
#ifdef TRACY_ENABLE
		TracyAlloc(ptr, size);
#endif
		return ptr;
	}

	void* _calloc(size_t size, const char* file, int line) override {
		(void)file;
		(void)line;
		auto ptr = std::calloc(size, 1);
#ifdef TRACY_ENABLE
		TracyAlloc(ptr, size);
#endif

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
#ifdef TRACY_ENABLE
		TracyFree(mem);
#endif
		std::free(mem);
	}

	char* _readFile(const String& path, int* length) override {
		// Basic file loader (UTF-8 safe, binary mode)
		std::vector<uint8_t> data;
		resource::ResourceManager::GetInstance()->OpenFile(path.buffer(), data);
		if (length) {
			*length = static_cast<int>(data.size());
		}
		return reinterpret_cast<char*>(data.data());
	}
};

SpineExtension* getDefaultExtension();

}    // namespace spine
