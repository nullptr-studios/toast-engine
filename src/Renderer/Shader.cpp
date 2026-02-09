//
// Created by dario on 17/09/2025.
//

#include "Toast/Renderer/Shader.hpp"

#include "Toast/Log.hpp"
#include "Toast/Resources/ResourceManager.hpp"

#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

///@TODO Improve error handling and log

namespace renderer {

Shader::Shader(std::string path) : IResource(path, resource::ResourceType::SHADER, true) { }

Shader::~Shader() {
	ClearProgram();
}

void Shader::Load() {
	PROFILE_ZONE;
	SetResourceState(resource::ResourceState::LOADING);
	// Parse Json
	std::istringstream in {};
	if (!resource::Open(m_path, in)) {
		TOAST_ERROR("Shader Failed to open shader file: {0}", m_path);
		SetResourceState(resource::ResourceState::FAILED);
		LoadErrorShader();
		return;
	}

	nlohmann::ordered_json j = nlohmann::ordered_json::parse(in, nullptr, false);
	if (j.is_discarded()) {
		TOAST_ERROR("Shader Failed to parse shader JSON file: {0}", m_path);
		SetResourceState(resource::ResourceState::FAILED);
		LoadErrorShader();
		return;
	}

	if (!j.contains("stageFiles") || !j["stageFiles"].is_array()) {
		TOAST_ERROR("Shader JSON file missing 'stageFiles' array: {0}", m_path);
		SetResourceState(resource::ResourceState::FAILED);
		LoadErrorShader();
		return;
	}

	std::vector<std::pair<Stage, std::filesystem::path>> stage_files;
	for (const auto& item : j["stageFiles"]) {
		if (!item.contains("stage") || !item.contains("path")) {
			TOAST_ERROR("Shader JSON 'stageFiles' item missing 'stage' or 'path': {0}", m_path);
			SetResourceState(resource::ResourceState::FAILED);
			LoadErrorShader();
			return;
		}
		std::string stage_str = item["stage"].get<std::string>();
		std::string file_path = item["path"].get<std::string>();
		Stage stage = Stage::Vertex;
		if (stage_str == "vertex") {
			stage = Stage::Vertex;
		} else if (stage_str == "fragment") {
			stage = Stage::Fragment;
		} else if (stage_str == "geometry") {
			stage = Stage::Geometry;
		} else if (stage_str == "compute") {
			stage = Stage::Compute;
		} else {
			TOAST_ERROR("Shader JSON unknown stage type: {0} in file {1}", stage_str, m_path);
			SetResourceState(resource::ResourceState::FAILED);
			LoadErrorShader();
			return;
		}
		stage_files.emplace_back(stage, std::filesystem::path(file_path));
	}
	if (stage_files.empty()) {
		TOAST_ERROR("Shader JSON file has no valid 'stageFiles': {0}", m_path);
		SetResourceState(resource::ResourceState::FAILED);
		LoadErrorShader();
		return;
	}
	CreateFromFiles(stage_files, m_path);
	SetResourceState(resource::ResourceState::LOADEDCPU);
}

// move logic
Shader::Shader(Shader&& other) noexcept
    : m_program(other.m_program),
      m_uniformLocationCache(std::move(other.m_uniformLocationCache)),
      m_sourceFiles(std::move(other.m_sourceFiles)),
      m_debugName(std::move(other.m_debugName)) {
	// invalidate other shader
	other.m_program = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
	if (this != &other) {
		ClearProgram();
		m_program = other.m_program;
		m_uniformLocationCache = std::move(other.m_uniformLocationCache);
		m_sourceFiles = std::move(other.m_sourceFiles);
		m_debugName = std::move(other.m_debugName);

		// invalidate other shader
		other.m_program = 0;
	}
	return *this;
}

void Shader::LoadMainThread() {
	PROFILE_ZONE;
	if (GetResourceState() == resource::ResourceState::UPLOADEDGPU) {
		// already loaded
		return;
	}
	SetResourceState(resource::ResourceState::UPLOADING);

	std::vector<GLuint> compiled;
	compiled.reserve(m_sourcesToLoad.size());
	// Compile all stages
	bool compilation_failed = false;
	for (auto& kv : m_sourcesToLoad) {
		GLuint shader = CompileSingleStage(kv.first, kv.second);
		if (shader == 0) {
			compilation_failed = true;
			break;
		}
		compiled.push_back(shader);
	}

	if (compilation_failed) {
		// Clean up any successfully compiled shaders
		for (auto sh : compiled) {
			if (sh != 0) {
				glDeleteShader(sh);
			}
		}
		TOAST_ERROR("Shader compilation failed, loading error shader");
		LoadErrorShader();
		return;
	}

	LinkProgram(compiled);
	
	// Check if linking succeeded
	if (!m_program) {
		TOAST_ERROR("Shader linking failed, loading error shader");
		LoadErrorShader();
		return;
	}
	
	SetResourceState(resource::ResourceState::UPLOADEDGPU);
}

void Shader::ClearProgram() {
	if (m_program) {
		glDeleteProgram(m_program);
		m_program = 0;
	}
	m_uniformLocationCache.clear();
}

// Compile single shader stage
GLuint Shader::CompileSingleStage(Stage stage, std::string_view source) const {
	PROFILE_ZONE;
	GLuint s = glCreateShader(static_cast<GLenum>(stage));

	// source shader code
	const char* src = source.data();
	GLint len = static_cast<GLint>(source.size());

	glShaderSource(s, 1, &src, &len);
	glCompileShader(s);

	GLint ok = GL_FALSE;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);

	// error checking
	if (ok == GL_FALSE) {
		GLint log_len = 0;
		glGetShaderiv(s, GL_INFO_LOG_LENGTH, &log_len);

		std::string log(log_len, '\0');
		glGetShaderInfoLog(s, log_len, nullptr, log.data());

		std::string error_msg = "Shader compile error (" + StageToString(stage) + "):\n" + log;
		glDeleteShader(s);

		TOAST_ERROR("{0}", error_msg);
		return 0; // Return 0 to indicate failure
	}
	return s;
}

void Shader::LinkProgram(const std::vector<GLuint>& shaders) {
	PROFILE_ZONE;
	
	// Check if any shader failed to compile (has id 0)
	for (auto sh : shaders) {
		if (sh == 0) {
			TOAST_ERROR("Cannot link program: one or more shaders failed to compile");
			return; // Don't attempt to link
		}
	}
	
	// Create attach and link all programs
	GLuint program = glCreateProgram();
	for (auto sh : shaders) {
		glAttachShader(program, sh);
	}
	glLinkProgram(program);

	GLint ok = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &ok);

	// error checking
	if (ok == GL_FALSE) {
		GLint log_len = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);

		std::string log(log_len, '\0');
		glGetProgramInfoLog(program, log_len, nullptr, log.data());

		for (auto sh : shaders) {
			glDeleteShader(sh);
		}
		glDeleteProgram(program);
		TOAST_ERROR("Program link error:\n{0}", log);
		return; // Don't throw, just return
	}

	// detach and delete shaders after successful link
	for (auto sh : shaders) {
		glDetachShader(program, sh);
		glDeleteShader(sh);
	}

	// delete previous program and replace
	ClearProgram();
	m_program = program;
	m_uniformLocationCache.clear();
}

void Shader::CreateFromSources(const std::vector<std::pair<Stage, std::string>>& stage_sources, std::string_view debug_name) {
	if (stage_sources.empty()) {
		TOAST_ERROR("No shader stages provided");
		LoadErrorShader();
		return;
	}

	m_debugName = std::string(debug_name);

	// STORE TO COMPILE IN MAIN THREAD
	m_sourcesToLoad = stage_sources;
}

void Shader::CreateFromFiles(const std::vector<std::pair<Stage, std::filesystem::path>>& stage_files, std::string_view debug_name) {
	PROFILE_ZONE;
	if (stage_files.empty()) {
		TOAST_ERROR("No shader files provided");
		LoadErrorShader();
		return;
	}

	m_debugName = std::string(debug_name);
	m_sourceFiles = stage_files;    // store for reload
	std::vector<std::pair<Stage, std::string>> loaded;
	loaded.reserve(stage_files.size());
	for (const auto& [stage, path] : stage_files) {
		std::istringstream in {};
		if (!resource::Open(path.string(), in)) {
			TOAST_ERROR("Shader Failed to open shader file: {0}", path.string());
			LoadErrorShader();
			return;
		}
		loaded.emplace_back(stage, std::move(in.str()));
	}

	// convert and call createFromSources
	std::vector<std::pair<Stage, std::string>> tmp = std::move(loaded);
	CreateFromSources(tmp, m_debugName);
}

void Shader::Reload() {
	if (m_sourceFiles.empty()) {
		TOAST_WARN("Shader reload not available: Shader not constructed from files!!");
	}

	std::vector<std::pair<Stage, std::string>> loaded;
	loaded.reserve(m_sourceFiles.size());
	for (auto& kv : m_sourceFiles) {
		std::istringstream in {};
		if (!resource::Open(kv.second.string(), in)) {
			TOAST_ERROR("Failed to open shader file: {0}", kv.second.string());
			return;
		}
		loaded.emplace_back(kv.first, std::move(in.str()));
	}

	CreateFromSources(loaded, m_debugName);
}

void Shader::Use() {
	if (!m_program) {
		// try to load
		//  this is kinda stoopid
		LoadMainThread();
		// throw ToastException(
		//     "Attempt to use invalid shader program: " + m_debugName +
		//     " \n Did you just created shader? wait for call LoadMainThread() on the main thread!"
		//);
	}
	glUseProgram(m_program);
}

// Uniform caching
GLint Shader::QueryUniformLocation(std::string_view name) {
	auto it = m_uniformLocationCache.find(std::string(name));
	if (it != m_uniformLocationCache.end()) {
		return it->second;
	}
	GLint loc = glGetUniformLocation(m_program, std::string(name).c_str());
	m_uniformLocationCache.emplace(std::string(name), loc);
	return loc;
}

GLint Shader::GetUniformLocation(std::string_view name) {
	if (!m_program) {
		throw ToastException("getUniformLocation called on invalid program");
	}
	return QueryUniformLocation(name);
}

GLint Shader::GetAttribLocation(std::string_view name) const {
	if (!m_program) {
		throw ToastException("getAttribLocation called on invalid program");
	}
	// try cache first
	auto it = m_attribLocationCache.find(std::string(name));
	if (it != m_attribLocationCache.end()) {
		return it->second;
	}
	GLint loc = glGetAttribLocation(m_program, std::string(name).c_str());
	m_attribLocationCache.emplace(std::string(name), loc);
	return loc;
}

void Shader::SetGenericAttrib(std::string_view name, const glm::vec4& v) {
	if (!m_program) {
		throw ToastException("SetGenericAttrib called on invalid program");
	}
	GLint loc = GetAttribLocation(name);
	if (loc < 0) {
		// attribute not found in program (optimized out or not declared) -> fallback to conventional location 3
		loc = 3;
	}
	// set generic attrib value for the current VAO (or when no VAO attrib array enabled)
	glVertexAttrib4f(static_cast<GLuint>(loc), v.r, v.g, v.b, v.a);
}

// Setters (examples for common types)
void Shader::Set(std::string_view name, int value) {
	GLint loc = QueryUniformLocation(name);
	if (loc >= 0) {
		glUniform1i(loc, value);
	}
}

void Shader::Set(std::string_view name, float value) {
	GLint loc = QueryUniformLocation(name);
	if (loc >= 0) {
		glUniform1f(loc, value);
	}
}

void Shader::Set(std::string_view name, const glm::vec2& v) {
	GLint loc = QueryUniformLocation(name);
	if (loc >= 0) {
		glUniform2fv(loc, 1, &v[0]);
	}
}

void Shader::Set(std::string_view name, const glm::vec3& v) {
	GLint loc = QueryUniformLocation(name);
	if (loc >= 0) {
		glUniform3fv(loc, 1, &v[0]);
	}
}

void Shader::Set(std::string_view name, const glm::vec4& v) {
	GLint loc = QueryUniformLocation(name);
	if (loc >= 0) {
		glUniform4fv(loc, 1, &v[0]);
	}
}

void Shader::Set(std::string_view name, const glm::mat3& m, bool transpose) {
	GLint loc = QueryUniformLocation(name);
	if (loc >= 0) {
		glUniformMatrix3fv(loc, 1, transpose ? GL_TRUE : GL_FALSE, &m[0][0]);
	}
}

void Shader::Set(std::string_view name, const glm::mat4& m, bool transpose) {
	GLint loc = QueryUniformLocation(name);
	if (loc >= 0) {
		glUniformMatrix4fv(loc, 1, transpose ? GL_TRUE : GL_FALSE, &m[0][0]);
	}
}

void Shader::SetSampler(std::string_view name, int texture_unit) {
	Set(name, texture_unit);
}

void Shader::SetUniformBlockBinding(std::string_view block_name, GLuint binding_point) const {
	GLuint index = glGetUniformBlockIndex(m_program, std::string(block_name).c_str());
	if (index == GL_INVALID_INDEX) {
		// silently ignore or print debug
		TOAST_WARN("Uniform block not found: {0}", block_name);
		return;
	}
	glUniformBlockBinding(m_program, index, binding_point);
}

std::string Shader::StageToString(Stage s) {
	switch (s) {
		case Stage::Vertex: return "Vertex";
		case Stage::Fragment: return "Fragment";
		case Stage::Geometry: return "Geometry";
		case Stage::Compute: return "Compute";
	}
	return "Unknown";
}

void Shader::LoadErrorShader() {
	PROFILE_ZONE;
	TOAST_WARN("Loading error fallback shader for shader: {0}", m_path);
	
	const char* vertexSource = R"(
#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec4 aTangent; // xyz = tangent, w = handedness

uniform mat4 gMVP;
uniform mat4 gWorld;

void main()
{
    gl_Position = gMVP * vec4(aPos, 1.0);
}
)";
	
	const char* fragmentSource = R"(
#version 460 core
out vec4 FragColor;

//in vec2 TexCoord;

void main(void)
{
    vec2 resolution = vec2(800.0f, 800.f);
    vec2 TexCoord = gl_FragCoord.st / resolution.xy;
	
    float x = floor(TexCoord.x * 8.0);
    float y = floor(TexCoord.y * 8.0);
    float pattern = mod(x + y, 2.0);
    vec3 color = mix(vec3(1.0f,0,0), vec3(0.0), pattern);
    FragColor = vec4(color, 1.0);
}
)";

	std::vector<std::pair<Stage, std::string>> errorStages;
	errorStages.emplace_back(Stage::Vertex, std::string(vertexSource));
	errorStages.emplace_back(Stage::Fragment, std::string(fragmentSource));
	
	m_debugName = "ErrorShader";
	m_sourceFiles.clear(); // Can't reload error shader from files
	
	CreateFromSources(errorStages, m_debugName);
	LoadMainThread();
	
	SetResourceState(resource::ResourceState::UPLOADEDGPU);
}
}

