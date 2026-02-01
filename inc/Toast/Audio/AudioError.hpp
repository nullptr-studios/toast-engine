/// @file AudioError.hpp
/// @author Xein
/// @date 30 Jan 2026

#pragma once

#include <cstdint>

namespace audio {

enum class AudioError : uint8_t {
	NotLoaded,
	AlreadyLoaded,
	NotPlaying,
	EventNotFound,
	BankNotLoaded,
	InitializationFailed
};

}    // namespace audio
