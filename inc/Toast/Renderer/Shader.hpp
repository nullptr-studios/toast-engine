/// @file Shader.hpp
/// @author Dario
/// @date 16/09/25

#pragma once
#include "Toast/Resources/IResource.hpp"

#include <filesystem>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace renderer {
/// @enum Stage
/// @brief Enum for the different shader stages
enum class Stage : uint16_t {
	Vertex = GL_VERTEX_SHADER,
	Fragment = GL_FRAGMENT_SHADER,
	Geometry = GL_GEOMETRY_SHADER,
	Compute = GL_COMPUTE_SHADER
};

/// @class Shader
/// @brief Class for handling OpenGL shaders
class Shader : public IResource {
public:
	Shader(std::string path);
	// non-copyable
	Shader(const Shader&) = delete;
	Shader& operator=(const Shader&) = delete;
	// movable
	Shader(Shader&& other) noexcept;
	Shader& operator=(Shader&& other) noexcept;

	~Shader() override;

	void Load() override;
	void LoadMainThread() override;

	/// @brief Create shader from sources (stage, source)
	void CreateFromSources(const std::vector<std::pair<Stage, std::string>>& stage_sources, std::string_view debug_name = {});

	/// @brief Create shader from files (stage, path)
	void CreateFromFiles(const std::vector<std::pair<Stage, std::filesystem::path>>& stage_files, std::string_view debug_name = {});

	/// @brief Recompile (only if constructed from files).
	void Reload();

	/// @brief Activate the shader
	void Use();

	// Deactivate the shader
	void unuse() {
		glUseProgram(0);
	}

	/// @return OpenGL program ID
	[[nodiscard]]
	GLuint id() const noexcept {
		return m_program;
	}

	/// @return true if the shader is valid
	[[nodiscard]]
	bool valid() const noexcept {
		return m_program != 0;
	}

	// Uniform setters
	void Set(std::string_view name, int value);
	void Set(std::string_view name, float value);
	void Set(std::string_view name, const glm::vec2& v);
	void Set(std::string_view name, const glm::vec3& v);
	void Set(std::string_view name, const glm::vec4& v);
	void Set(std::string_view name, const glm::mat3& m, bool transpose = false);
	void Set(std::string_view name, const glm::mat4& m, bool transpose = false);

	// Generic sampler bind
	void SetSampler(std::string_view name, int texture_unit);

	// Uniform block binding
	void SetUniformBlockBinding(std::string_view block_name, GLuint binding_point) const;

	// Generic attribute setter
	void SetGenericAttrib(std::string_view name, const glm::vec4& v);

	// Explicit query utility
	[[nodiscard]]
	GLint GetAttribLocation(std::string_view name) const;
	GLint GetUniformLocation(std::string_view name);

	// Debug name
	[[nodiscard]]
	std::string debugName() const {
		return m_debugName;
	}

private:
	GLuint m_program = 0;
	std::unordered_map<std::string, GLint> m_uniformLocationCache;
	// Attribute location cache
	mutable std::unordered_map<std::string, GLint> m_attribLocationCache;
	// Files used for creation
	std::vector<std::pair<Stage, std::filesystem::path>> m_sourceFiles;
	std::vector<std::pair<Stage, std::string>> m_sourcesToLoad;

	std::string m_debugName;

	// helpers
	[[nodiscard]]
	GLuint CompileSingleStage(Stage stage, std::string_view source) const;
	void LinkProgram(const std::vector<GLuint>& shaders);
	GLint QueryUniformLocation(std::string_view name);
	void ClearProgram();
	static std::string StageToString(Stage s);
};
};
