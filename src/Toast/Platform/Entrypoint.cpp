#include <Toast/Entrypoint.hpp>

#if defined(_WIN32) || defined(_WIN64)
#include "Windows.inl"
#elif defined(__linux__)
#include "Linux.inl"
#elif defined(__APPLE__)
#include "MacOS.inl"
#else
#error "OS not supported by Toast Engine"
#endif
