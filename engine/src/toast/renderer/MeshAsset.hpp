/// @file MeshAsset.hpp
/// @author dario
/// @date 08/06/2026.

#pragma once

#include "core/VulkanMesh.hpp"

#include <vector>

/// @brief Stores mesh geometry data along with GPU resources
struct MeshAsset {
	toast::renderer::VulkanMesh mesh;

	std::vector<toast::renderer::Vertex> vertices;
	std::vector<uint32_t> indices;

	// TODO: future asset handling
};

/// @brief Represents an instance of a mesh ready for rendering
/// @todo Change to actual MeshNode
struct MeshInstance    // instance for render
{
	MeshAsset* mesh = nullptr;
	// MATERIAL
	// TRANSFORM
};
