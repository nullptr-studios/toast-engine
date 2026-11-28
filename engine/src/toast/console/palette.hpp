/**
 * @file palette.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief The editor's colors (editor/Resources/EditorColors.axaml)
 */

#pragma once
#include <cstdint>

namespace toast::console::palette {

struct Color {
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

inline constexpr Color k_bg1 {0x12, 0x11, 0x11};
inline constexpr Color k_bg2 {0x1F, 0x1D, 0x1E};
inline constexpr Color k_bg3 {0x2B, 0x2A, 0x2A};
inline constexpr Color k_bg4 {0x38, 0x36, 0x37};
inline constexpr Color k_bg5 {0x45, 0x43, 0x43};

inline constexpr Color k_text {0xFA, 0xFA, 0xFA};
inline constexpr Color k_text_muted {0xA0, 0xA0, 0xA0};

inline constexpr Color k_red {0xFF, 0x16, 0x59};
inline constexpr Color k_green {0x00, 0xFF, 0x40};
inline constexpr Color k_blue {0x29, 0xAD, 0xFF};
inline constexpr Color k_magenta {0xAD, 0x73, 0xFF};
inline constexpr Color k_orange {0xFF, 0xA3, 0x00};
inline constexpr Color k_cyan {0x73, 0xFF, 0xEA};

}
