/// @file DebugDrawLayer.cpp
/// @author dario
/// @date 07/10/2025.

#include "Toast/Renderer/OclussionVolume.hpp"
#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

#include "Toast/Renderer/DebugDrawLayer.hpp"
#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Resources/ResourceManager.hpp"

#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>

namespace renderer {

DebugDrawLayer* DebugDrawLayer::m_instance = nullptr;

DebugDrawLayer::DebugDrawLayer() : ILayer("Debug Draw Layer") {
	if (m_instance == nullptr) {
		m_instance = this;
	}
}

DebugDrawLayer::~DebugDrawLayer() = default;

void DebugDrawLayer::OnAttach() {
	// create shader
	m_shader = resource::ResourceManager::GetInstance()->LoadResource<renderer::Shader>("shaders/debug.shader");

	// create VAO/VBO
	glGenVertexArrays(1, &m_vao);
	glGenBuffers(1, &m_vbo);

	glBindVertexArray(m_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	// allocate some space initially
	glBufferData(GL_ARRAY_BUFFER, 1024 * sizeof(DebugVertex), nullptr, GL_DYNAMIC_DRAW);

	// position (vec2)
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(DebugVertex), reinterpret_cast<void*>(offsetof(DebugVertex, pos)));
	// color (vec4)
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(DebugVertex), reinterpret_cast<void*>(offsetof(DebugVertex, color)));

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	// reserve to avoid reallocation very often
	m_vertices.reserve(4096*3);

	m_renderer = IRendererBase::GetInstance();
}

void DebugDrawLayer::OnDetach() { }

void DebugDrawLayer::OnTick() { }

void DebugDrawLayer::OnRender() {
	if (m_vertices.empty() || !m_enabled) {
		return;
	}

	// Render on top:
	// GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
	// if (depthTestEnabled == GL_TRUE) {
	//	glDisable(GL_DEPTH_TEST);
	//}

	// glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// glLineWidth(1.5f);

	Flush();

	// restore depth-test state
	// if (depthTestEnabled == GL_TRUE) {
	//	glEnable(GL_DEPTH_TEST);
	//}

	// clear
	m_vertices.clear();
}

void DebugDrawLayer::Flush() const {
	glBindVertexArray(m_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

	size_t byte_size = m_vertices.size() * sizeof(DebugVertex);

	static size_t capacity_bytes = 1024 * sizeof(DebugVertex);
	if (byte_size > capacity_bytes) {
		capacity_bytes = byte_size;
		glBufferData(GL_ARRAY_BUFFER, capacity_bytes, nullptr, GL_STREAM_DRAW);
	}
	if (byte_size > 0) {
		glBufferSubData(GL_ARRAY_BUFFER, 0, byte_size, m_vertices.data());
	}

	m_shader->Use();
	m_shader->Set("transform", m_renderer->GetViewProjectionMatrix());

	// draw as lines
	glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(m_vertices.size()));

	// unbind
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
	glUseProgram(0);
}

void DebugDrawLayer::DrawLine(const glm::vec2& a, const glm::vec2& b, const glm::vec4& color) {
	if (!m_enabled) {
		return;
	}
	m_vertices.push_back({ glm::vec3(a.x, a.y, 0.f), color });
	m_vertices.push_back({ glm::vec3(b.x, b.y, 0.f), color });
}

void DebugDrawLayer::DrawLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color) {
	if (!m_enabled) {
		return;
	}
	m_vertices.push_back({ a, color });
	m_vertices.push_back({ b, color });
}

void DebugDrawLayer::DrawRect(const glm::vec2& pos, const glm::vec2& size, const glm::vec4& color) {
	if (!m_enabled) {
		return;
	}
	// pos is center position
	glm::vec2 half_size = size * 0.5f;
	glm::vec2 a = pos + glm::vec2(-half_size.x, -half_size.y);
	glm::vec2 b = pos + glm::vec2(half_size.x, -half_size.y);
	glm::vec2 c = pos + glm::vec2(half_size.x, half_size.y);
	glm::vec2 d = pos + glm::vec2(-half_size.x, half_size.y);

	DrawLine(a, b, color);
	DrawLine(b, c, color);
	DrawLine(c, d, color);
	DrawLine(d, a, color);
}

void DebugDrawLayer::DrawRect(const glm::vec3& pos, const glm::vec3& size, float rotation, const glm::vec4& color) {
	if (!m_enabled) {
		return;
	}
	glm::vec2 half_size = size * 0.5f;
	auto rotatemxt = glm::rotate(glm::mat4(1.0f), rotation, glm::vec3(0.0f, 0.0f, 1.0f));

	glm::vec3 a = glm::vec3(-half_size.x, -half_size.y, 0.f);
	glm::vec3 b = glm::vec3(half_size.x, -half_size.y, 0.f);
	glm::vec3 c = glm::vec3(half_size.x, half_size.y, 0.f);
	glm::vec3 d = glm::vec3(-half_size.x, half_size.y, .0f);
	a = glm::vec3(rotatemxt * glm::vec4(a, 1.0f)) + pos;
	b = glm::vec3(rotatemxt * glm::vec4(b, 1.0f)) + pos;
	c = glm::vec3(rotatemxt * glm::vec4(c, 1.0f)) + pos;
	d = glm::vec3(rotatemxt * glm::vec4(d, 1.0f)) + pos;

	DrawLine(a, b, color);
	DrawLine(b, c, color);
	DrawLine(c, d, color);
	DrawLine(d, a, color);
}

void DebugDrawLayer::DrawCircle(const glm::vec2& center, float radius, const glm::vec4& color, int segments) {
	if (!m_enabled) {
		return;
	}
	if (segments <= 0) {
		segments = glm::clamp<int>(static_cast<int>(radius * 0.15f * 16.0f), 12, 128);
	}

	double step = glm::two_pi<double>() / static_cast<double>(segments);
	glm::vec2 prev = center + glm::vec2(radius, 0.0f);
	for (int i = 1; i <= segments; ++i) {
		double ang = step * static_cast<double>(i);
		glm::vec2 cur = center + glm::vec2(cosf(ang) * radius, sinf(ang) * radius);
		DrawLine(prev, cur, color);
		prev = cur;
	}
}

void DebugDrawLayer::DrawCircle(const glm::vec3& center, float radius, const glm::vec4& color, int segments) {
	if (!m_enabled) {
		return;
	}
	if (segments <= 0) {
		segments = glm::clamp<int>(static_cast<int>(radius * 0.15f * 16.0f), 12, 128);
	}

	double step = glm::two_pi<double>() / static_cast<double>(segments);
	glm::vec2 prev = center + glm::vec3(radius, 0.0f, 0.0f);
	for (int i = 1; i <= segments; ++i) {
		double ang = step * static_cast<double>(i);
		glm::vec2 cur = center + glm::vec3(cosf(ang) * radius, sinf(ang) * radius, 0.0f);
		DrawLine(prev, cur, color);
		prev = cur;
	}
}

void DebugDrawLayer::DrawPoly(const std::vector<glm::vec2>& points, const glm::vec4& color, bool closed) {
	if (!m_enabled) {
		return;
	}
	if (points.size() < 2) {
		return;
	}
	for (size_t i = 0; i + 1 < points.size(); ++i) {
		DrawLine(points[i], points[i + 1], color);
	}
	if (closed && points.size() > 2) {
		DrawLine(points.back(), points.front(), color);
	}
}

void DebugDrawLayer::DrawPoly(const std::vector<glm::vec3>& points, const glm::vec4& color, bool closed) {
	if (!m_enabled) {
		return;
	}
	if (points.size() < 2) {
		return;
	}
	for (size_t i = 0; i + 1 < points.size(); ++i) {
		DrawLine(points[i], points[i + 1], color);
	}
	if (closed && points.size() > 2) {
		DrawLine(points.back(), points.front(), color);
	}
}

///@TODO
void DebugDrawLayer::DrawGrid(float gridSize, glm::mat4 viewProjection) {
	if (!m_enabled) {
		return;
	}
	// glm::mat4 res = glm::mat4(1.f) * viewProjection;

	// glm::mat4 grid_model = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1, 0, 0));

	//@TODO: Fix grid drawing with proper frustum culling

	glm::mat4 res = viewProjection /* grid_model*/;

	glm::mat4 rot = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0));

	for (float f = -gridSize; f <= gridSize; f += 1.f) {
		for (int dir = 0; dir < 2; dir++) {
			glm::vec3 ptA = glm::vec3(dir ? -gridSize : f, 0.f, dir ? f : -gridSize);
			glm::vec3 ptB = glm::vec3(dir ? gridSize : f, 0.f, dir ? f : gridSize);
			bool visible = true;
			for (int i = 0; i < 6; i++) {
				// if (!OclussionVolume::isSphereOnPlanes(IRendererBase::GetInstance()->GetFrustumPlanes(), ptA, 0)
				//	&& !OclussionVolume::isSphereOnPlanes(IRendererBase::GetInstance()->GetFrustumPlanes(), ptB, 0))
				//{
				//	visible = false;
				//	break;
				// }
				// if (dA > 0.f && dB > 0.f)
				//{
				//	continue;
				// }
				// if (dA < 0.f)
				//{
				//	float len = fabsf(dA - dB);
				//	float t = fabsf(dA) / len;
				//	ptA.Lerp(ptB, t);
				// }
				// if (dB < 0.f)
				//{
				//	float len = fabsf(dB - dA);
				//	float t = fabsf(dB) / len;
				//	ptB.Lerp(ptA, t);
				// }
			}
			if (visible) {
#ifdef TOAST_EDITOR
				ImU32 col = IM_COL32(0x80, 0x80, 0x80, 0xFF);
				col = (fmodf(fabsf(f), 10.f) < FLT_EPSILON) ? IM_COL32(0x90, 0x90, 0x90, 0xFF) : col;
				col = (fabsf(f) < FLT_EPSILON) ? IM_COL32(0x40, 0x40, 0x40, 0xFF) : col;

				float thickness = 1.f;
				thickness = (fmodf(fabsf(f), 10.f) < FLT_EPSILON) ? 1.5f : thickness;
				thickness = (fabsf(f) < FLT_EPSILON) ? 2.3f : thickness;

				ptA = glm::vec3(rot * glm::vec4(ptA, 1.f));
				ptB = glm::vec3(rot * glm::vec4(ptB, 1.f));

				renderer::DebugLine(
				    ptA,
				    ptB,
				    glm::vec4(
				        static_cast<float>((col & 0xFF0000) >> 16) / 255.f,
				        static_cast<float>((col & 0x00FF00) >> 8) / 255.f,
				        static_cast<float>(col & 0x0000FF) / 255.f,
				        static_cast<float>((col & 0xFF000000) >> 24) / 255.f
				    )
				);
#endif
			}
		}
	}
}

void DebugDrawLayer::DrawFrustum(std::array<glm::vec4, 6> planes) {
	// TODO
}

void DebugDrawLayer::Clear() {
	m_vertices.clear();
}

}
