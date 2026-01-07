/// @file DebugDrawLayer.hpp
/// @author dario
/// @date 06/10/2025.

#pragma once
#include <Toast/Renderer/ILayer.hpp>
#include <Toast/Renderer/Shader.hpp>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

namespace renderer {
class IRendererBase;

struct DebugVertex {
	glm::vec3 pos;
	glm::vec4 color;
};

/// @class DebugDrawLayer
/// @brief Draws debug shapes on top of everything
class DebugDrawLayer : public ILayer {
public:
	DebugDrawLayer();
	~DebugDrawLayer() override;

	void OnAttach() override;
	void OnDetach() override;
	void OnTick() override;
	void OnRender() override;

	///@brief Draws a simple line
	///@param a Start point
	///@param b End point
	///@param color Color of the line (default white)
	void DrawLine(const glm::vec2& a, const glm::vec2& b, const glm::vec4& color = { 1, 1, 1, 1 });
	void DrawLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color = { 1, 1, 1, 1 });

	///@brief Draws a rectangle
	///@param pos Center position
	///@param size Size of the rectangle
	///@param color Color of the rectangle (default white)
	void DrawRect(const glm::vec2& pos, const glm::vec2& size, const glm::vec4& color = { 1, 1, 1, 1 });
	void DrawRect(const glm::vec3& pos, const glm::vec3& size, float rotation = 0, const glm::vec4& color = { 1, 1, 1, 1 });

	///@brief Draws a circle
	///@param center Center position
	///@param radius Radius of the circle
	///@param color Color of the circle (default white)
	///@param segments Number of segments to use (default 0 = auto)
	void DrawCircle(const glm::vec2& center, float radius, const glm::vec4& color = { 1, 1, 1, 1 }, int segments = 0);
	void DrawCircle(const glm::vec3& center, float radius, const glm::vec4& color = { 1, 1, 1, 1 }, int segments = 0);

	///@brief Draws a polygon shape
	///@param points List of points
	///@param color Color of the polygon (default white)
	///@param closed Close the polygon (default true)
	void DrawPoly(const std::vector<glm::vec2>& points, const glm::vec4& color = { 1, 1, 1, 1 }, bool closed = true);
	void DrawPoly(const std::vector<glm::vec3>& points, const glm::vec4& color = { 1, 1, 1, 1 }, bool closed = true);

	void DrawGrid(float gridSize = 2.0f, glm::mat4 viewProjection = glm::mat4(1.0f));

	// TODO
	void DrawFrustum(std::array<glm::vec4, 6> planes);

	void Clear();

	void SetEnabled(bool enabled) {
		m_enabled = enabled;
	}

	[[nodiscard]]
	static DebugDrawLayer* GetInstance() {
		return m_instance;
	}

private:
	static DebugDrawLayer* m_instance;
	void Flush() const;    // upload and render
	GLuint m_vao = 0, m_vbo = 0;
	GLint m_projLocation = -1;

	IRendererBase* m_renderer = nullptr;
	std::shared_ptr<Shader> m_shader;

	std::vector<DebugVertex> m_vertices;

	bool m_enabled = true;
};

inline void DebugLine(const glm::vec2& a, const glm::vec2& b, const glm::vec4& color = { 1, 1, 1, 1 }) {
	DebugDrawLayer::GetInstance()->DrawLine(a, b, color);
}

inline void DebugLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color = { 1, 1, 1, 1 }) {
	DebugDrawLayer::GetInstance()->DrawLine(a, b, color);
}

inline void DebugCircle(const glm::vec2& center, float radius, const glm::vec4& color = { 1, 1, 1, 1 }, int segments = 16) {
	DebugDrawLayer::GetInstance()->DrawCircle(center, radius, color, segments);
}

inline void DebugCircle(const glm::vec3& center, float radius, const glm::vec4& color = { 1, 1, 1, 1 }, int segments = 16) {
	DebugDrawLayer::GetInstance()->DrawCircle(center, radius, color, segments);
}

inline void DebugRect(const glm::vec2& pos, const glm::vec2& size, const glm::vec4& color = { 1, 1, 1, 1 }) {
	DebugDrawLayer::GetInstance()->DrawRect(pos, size, color);
}

inline void DebugRect(const glm::vec3& pos, const glm::vec3& size, float rotation = 0, const glm::vec4& color = { 1, 1, 1, 1 }) {
	DebugDrawLayer::GetInstance()->DrawRect(pos, size, rotation, color);
}

inline void DebugPoly(const std::vector<glm::vec2>& points, const glm::vec4& color = { 1, 1, 1, 1 }, bool closed = true) {
	DebugDrawLayer::GetInstance()->DrawPoly(points, color, closed);
}

inline void DebugPoly(const std::vector<glm::vec3>& points, const glm::vec4& color = { 1, 1, 1, 1 }, bool closed = true) {
	DebugDrawLayer::GetInstance()->DrawPoly(points, color, closed);
}

}
