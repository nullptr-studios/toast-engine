/// @file MeshAsset.hpp
/// @author dario
/// @date 08/06/2026.

#pragma once

#include "VulkanMesh.hpp"

#include <vector>

struct MeshAsset {
	toast::renderer::VulkanMesh mesh;

	std::vector<toast::renderer::Vertex> vertices;
	std::vector<uint32_t> indices;

	// TODO: future asset handling
};

struct MeshInstance    // instance for render
{
	MeshAsset* mesh = nullptr;
	// MATERIAL
	// TRANSFORM
};
