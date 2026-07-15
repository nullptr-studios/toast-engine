/**
 * @file mesh.hpp
 * @author Xein
 * @date 11 Jun 2026
 *
 * @brief Represents 3D geometry data, can be read from a .tmesh binary file
 */

#pragma once
#include "core_types.hpp"
#include "toast/renderer/vertex.hpp"

#include <glm/glm.hpp>
#include <memory>
#include <toast/export.hpp>
#include <toast/log.hpp>

namespace toast::renderer {
class VulkanMesh;
}

namespace assets {

namespace _detail {
struct MeshFileHeader {
	std::array<uint8_t, 6> magic = {'T', 'M', 'E', 'S', 'H', '\0'};
	uint16_t version = 1;
	uint32_t vertex_count = 0;
	uint32_t index_count = 0;
};

}

class TOAST_API Mesh final : public Asset {
public:
	using Index = uint32_t;

	explicit Mesh(const std::vector<uint8_t>& data);
	Mesh(std::string_view name, std::vector<toast::renderer::Vertex>&& vertices, std::vector<uint32_t>&& indices);
	~Mesh() override;

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "mesh";
	}

	[[nodiscard]]
	auto vertices() const -> const std::vector<toast::renderer::Vertex>& {
		return m_vertices;
	}

	[[nodiscard]]
	auto indices() const -> const std::vector<Index>& {
		return m_indices;
	}

	[[nodiscard]]
	auto gpuMesh() const -> const toast::renderer::VulkanMesh&;

	[[nodiscard]]
	auto gpuMesh() -> toast::renderer::VulkanMesh&;

	[[nodiscard]]
	auto name() const -> const std::string& {
		return m_name;
	}

	[[nodiscard]]
	auto toBinary() const -> std::vector<uint8_t>;

private:
	std::string m_name;
	std::vector<toast::renderer::Vertex> m_vertices;
	std::vector<Index> m_indices;

	std::unique_ptr<toast::renderer::VulkanMesh> m_gpu_mesh;
};

}
