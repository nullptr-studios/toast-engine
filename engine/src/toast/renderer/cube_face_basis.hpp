/**
 * @file cube_face_basis.hpp
 * @author dario
 * @date 05/08/2026
 */

#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace renderer {

struct CubeFaceBasis {
	glm::vec3 right;
	glm::vec3 up;
	glm::vec3 forward;
};

/// @brief +X -X +Y -Y +Z -Z order. Must agree with directionForFace() in environment.slang
[[nodiscard]]
inline auto cubeFaceBasis(uint32_t face) -> CubeFaceBasis {
	switch (face) {
		case 0:
			return {
			  {0.0f,  0.0f, -1.0f},
        {0.0f, -1.0f,  0.0f},
        {1.0f,  0.0f,  0.0f}
			};
		case 1:
			return {
			  { 0.0f,  0.0f, 1.0f},
        { 0.0f, -1.0f, 0.0f},
        {-1.0f,  0.0f, 0.0f}
			};
		case 2:
			return {
			  {1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 0.0f}
			};
		case 3:
			return {
			  {1.0f,  0.0f,  0.0f},
        {0.0f,  0.0f, -1.0f},
        {0.0f, -1.0f,  0.0f}
			};
		case 4:
			return {
			  {1.0f,  0.0f, 0.0f},
        {0.0f, -1.0f, 0.0f},
        {0.0f,  0.0f, 1.0f}
			};
		default:
			return {
			  {-1.0f,  0.0f,  0.0f},
        { 0.0f, -1.0f,  0.0f},
        { 0.0f,  0.0f, -1.0f}
			};
	}
}

}
