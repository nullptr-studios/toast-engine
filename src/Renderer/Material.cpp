/// @file Material.cpp
/// @author dario
/// @date 05/11/2025.

#include "Toast/Renderer/Material.hpp"

#include "Toast/GlmJson.hpp"
#include "Toast/Resources/ResourceManager.hpp"
#include "Toast/Time.hpp"

#include <algorithm>
#include <any>
#include <typeinfo>

#ifdef TOAST_EDITOR
#include <imgui.h>
#endif

using json_t = nlohmann::ordered_json;

namespace renderer {

Material::Material(std::string_view path)
    : m_materialPath(path),
      m_shader(nullptr),
      IResource(path.data(), resource::ResourceType::MATERIAL, true) { }

Material::~Material() {
#ifdef TOAST_EDITOR
	for (auto* slot : m_textureSlots) {
		delete slot;
	}
	m_textureSlots.clear();

	if (m_shaderSlot) {
		delete m_shaderSlot;
		m_shaderSlot = nullptr;
	}
#endif

	// m_textures.clear();
	// m_shader = nullptr;
}

void Material::Load() {
	try {
		LoadMaterial();
	} catch (const std::exception& e) {
		TOAST_ERROR("Material load failed: {0}", e.what());
		SetResourceState(resource::ResourceState::FAILED);
		LoadErrorMaterial();
	}
}

void Material::LoadMainThread() {
	try {
		LoadResources();
	} catch (const std::exception& e) {
		TOAST_ERROR("Material resource load failed: {0}", e.what());
		SetResourceState(resource::ResourceState::FAILED);
		LoadErrorMaterial();
	}
}

void Material::ShowEditor() {
#ifdef TOAST_EDITOR
	if (!m_shaderSlot) {
		UpdateEditorSlots();
	}

	if (m_shaderSlot) {
		ImGui::Separator();
		ImGui::TextUnformatted("Shader");
		ImGui::SameLine();
		m_shaderSlot->Show();
	}

	// If a shader reload was requested by the slot callback
	if (m_pendingReloadShader) {
		m_pendingReloadShader = false;
		// Reload material
		LoadMaterial();
		LoadResources();
		UpdateEditorSlots();
	}

	// Render texture slots and other shader parameters together in order
	int tex_slot_index = 0;

	for (size_t i = 0; i < m_shaderParameters.size() && i < m_parameters.size(); ++i) {
		const auto& shader_param = m_shaderParameters[i];
		const std::string label = shader_param.name;

		ImGui::PushID(static_cast<int>(i));
		ImGui::Separator();
		ImGui::TextUnformatted(label.c_str());
		// ImGui::SameLine();
		// ImGui::TextDisabled("[%s]", shaderParam.type.c_str());

		if (shader_param.type == "texture") {
			if (tex_slot_index < m_textureSlots.size() && m_textureSlots[tex_slot_index]) {
				m_textureSlots[tex_slot_index]->Show();
			} else {
				ImGui::TextDisabled("(no texture slot)");
			}
			++tex_slot_index;

		} else if (shader_param.type == "vec4") {
			glm::vec4 v(1.0f);
			if (m_parameters[i].second.type() == typeid(glm::vec4)) {
				v = std::any_cast<glm::vec4>(m_parameters[i].second);
			}
			if (ImGui::DragFloat4("Vec4 Value", &v.x, 0.01f)) {
				m_parameters[i].second = v;
				SaveMaterial();
			}

		} else if (shader_param.type == "vec3") {
			glm::vec3 v(1.0f);
			if (m_parameters[i].second.type() == typeid(glm::vec3)) {
				v = std::any_cast<glm::vec3>(m_parameters[i].second);
			}
			if (ImGui::DragFloat3("Vec3 Value", &v.x, 0.01f)) {
				m_parameters[i].second = v;
				SaveMaterial();
			}

		} else if (shader_param.type == "float") {
			float f = 1.0f;
			if (m_parameters[i].second.type() == typeid(float)) {
				f = std::any_cast<float>(m_parameters[i].second);
			}
			if (ImGui::DragFloat("Float Value", &f, 0.01f)) {
				m_parameters[i].second = f;
				SaveMaterial();
			}

		} else if (shader_param.type == "mat4") {
			glm::mat4 m(1.0f);
			if (m_parameters[i].second.type() == typeid(glm::mat4)) {
				m = std::any_cast<glm::mat4>(m_parameters[i].second);
			}
			// show 4 rows of 4 floats
			float row[4];
			bool changed = false;
			ImGui::TextDisabled("Matrix4");
			for (int r = 0; r < 4; ++r) {
				row[0] = m[r][0];
				row[1] = m[r][1];
				row[2] = m[r][2];
				row[3] = m[r][3];
				std::string rowLabel = std::string("row ") + std::to_string(r);
				if (ImGui::DragFloat4(rowLabel.c_str(), row, 0.01f)) {
					for (int c = 0; c < 4; ++c) {
						m[r][c] = row[c];
					}
					changed = true;
				}
			}
			if (changed) {
				m_parameters[i].second = m;
				SaveMaterial();
			}

		} else if (shader_param.type == "color") {
			glm::vec4 c(1.0f);
			if (m_parameters[i].second.type() == typeid(glm::vec4)) {
				c = std::any_cast<glm::vec4>(m_parameters[i].second);
			}
			if (ImGui::ColorEdit4("Color Value", &c.x)) {
				m_parameters[i].second = c;
				SaveMaterial();
			}
		} else {
			ImGui::TextDisabled("Unsupported parameter type");
		}

		ImGui::PopID();
	}
#endif
}

void Material::LoadMaterial() {
	m_shaderParameters.clear();
	m_parameters.clear();
	m_textures.clear();
	m_shader = nullptr;

	std::optional<std::string> data = resource::Open(m_materialPath);
	if (!data.has_value()) {
		throw ToastException("Failed to open material file: " + m_materialPath);
	}
	json_t j = json_t::parse(data.value());

	if (j.contains("shaderPath")) {
		m_shaderPath = j.at("shaderPath").get<std::string>();
	}

	// Read shader description to get parameter list
	data = resource::Open(m_shaderPath);
	if (!data.has_value()) {
		throw ToastException("Failed to open shader file: " + m_shaderPath);
	}
	json_t shaderJson = json_t::parse(data.value());

	if (shaderJson.contains("parameters")) {
		for (const auto& param : shaderJson.at("parameters")) {
			ShaderParameter shader_param;
			shader_param.name = param.at("name").get<std::string>();
			shader_param.type = param.at("type").get<std::string>();
			if (param.contains("defaultValue")) {
				shader_param.defaultValue = param.at("defaultValue").get<std::string>();
			}
			m_shaderParameters.push_back(shader_param);
		}
	}

	// If material has explicit materialParams use them, if not, initialize defaults
	if (j.contains("materialParams")) {
		unsigned idx = 0;
		for (const auto& param : j.at("materialParams")) {
			if (idx >= m_shaderParameters.size()) {
				break;
			}

			const auto& shaderType = m_shaderParameters[idx].type;
			const auto& shaderName = m_shaderParameters[idx].name;

			if (shaderType == "texture") {
				std::string s = param.get<std::string>();
				if (s.empty()) {
					s = m_shaderParameters[idx].defaultValue;
				}
				m_parameters.emplace_back(shaderName, s);
			} else if (shaderType == "vec4") {
				m_parameters.emplace_back(shaderName, param.get<glm::vec4>());
			} else if (shaderType == "vec3") {
				m_parameters.emplace_back(shaderName, param.get<glm::vec3>());
			} else if (shaderType == "float") {
				m_parameters.emplace_back(shaderName, param.get<float>());
			} else if (shaderType == "mat4") {
				m_parameters.emplace_back(shaderName, param.get<glm::mat4>());
			} else if (shaderType == "color") {
				m_parameters.emplace_back(shaderName, param.get<glm::vec4>());
			} else {
				// Unknown type
				m_parameters.emplace_back(shaderName, std::any {});
			}

			++idx;
		}

		// initialize remaining defaults
		for (size_t i = j.at("materialParams").size(); i < m_shaderParameters.size(); ++i) {
			const auto& shader_type = m_shaderParameters[i].type;
			const auto& shader_name = m_shaderParameters[i].name;
			if (shader_type == "texture") {
				m_parameters.emplace_back(shader_name, m_shaderParameters[i].defaultValue);
			} else if (shader_type == "vec4") {
				m_parameters.emplace_back(shader_name, glm::vec4 { 1.0f });
			} else if (shader_type == "vec3") {
				m_parameters.emplace_back(shader_name, glm::vec3 { 1.0f });
			} else if (shader_type == "float") {
				m_parameters.emplace_back(shader_name, 1.0f);
			} else if (shader_type == "mat4") {
				m_parameters.emplace_back(shader_name, glm::mat4 { 1.0f });
			} else if (shader_type == "color") {
				m_parameters.emplace_back(shader_name, glm::vec4 { 1.0f });
			} else {
				m_parameters.emplace_back(shader_name, std::any {});
			}
		}
	} else {
		// create defaults
		for (const auto& shader_param : m_shaderParameters) {
			if (shader_param.type == "texture") {
				m_parameters.emplace_back(shader_param.name, shader_param.defaultValue);
			} else if (shader_param.type == "vec4") {
				m_parameters.emplace_back(shader_param.name, glm::vec4 { 1.0f });
			} else if (shader_param.type == "vec3") {
				m_parameters.emplace_back(shader_param.name, glm::vec3 { 1.0f });
			} else if (shader_param.type == "float") {
				m_parameters.emplace_back(shader_param.name, 1.0f);
			} else if (shader_param.type == "mat4") {
				m_parameters.emplace_back(shader_param.name, glm::mat4 { 1.0f });
			} else if (shader_param.type == "color") {
				m_parameters.emplace_back(shader_param.name, glm::vec4 { 1.0f });
			} else {
				m_parameters.emplace_back(shader_param.name, std::any {});
			}
		}
	}

#ifdef TOAST_EDITOR
	UpdateEditorSlots();
#endif
}

void Material::SaveMaterial() {
	json_t j = json_t::object();
	j["materialParams"] = json_t::array();
	for (const auto& param : m_parameters) {
		try {
			if (param.second.type() == typeid(std::string)) {
				j["materialParams"].push_back(std::any_cast<std::string>(param.second));
			} else if (param.second.type() == typeid(glm::vec4)) {
				j["materialParams"].push_back(std::any_cast<glm::vec4>(param.second));
			} else if (param.second.type() == typeid(glm::vec3)) {
				j["materialParams"].push_back(std::any_cast<glm::vec3>(param.second));
			} else if (param.second.type() == typeid(float)) {
				j["materialParams"].push_back(std::any_cast<float>(param.second));
			} else if (param.second.type() == typeid(glm::mat4)) {
				j["materialParams"].push_back(std::any_cast<glm::mat4>(param.second));
			} else {
				// Unknown or empty -> push null
				j["materialParams"].push_back(nullptr);
			}
		} catch (const std::bad_any_cast&) { j["materialParams"].push_back(nullptr); }
	}

	j["shaderPath"] = m_shaderPath;

	resource::ResourceManager::SaveFile(m_materialPath, j.dump(2));
}

void Material::LoadResources() {
	m_textures.clear();

	// Load textures for every parameter that is a string (texture path)
	for (const auto& texturePath : m_parameters) {
		if (texturePath.second.type() != typeid(std::string)) {
			continue;
		}
		const auto& path = std::any_cast<std::string>(texturePath.second);
		if (path.empty()) {
			m_textures.push_back(nullptr);
			continue;
		}

		if (auto texture = resource::LoadResource<Texture>(path)) {
			m_textures.push_back(texture);
		} else {
			TOAST_WARN("Could not load texture at path: {0}", path);
			m_textures.push_back(nullptr);
		}
	}

	// Load shader resource
	if (!m_shaderPath.empty()) {
		m_shader = resource::LoadResource<renderer::Shader>(m_shaderPath);
		if (!m_shader) {
			TOAST_ERROR("Could not load shader at path: {0}, using error shader", m_shaderPath);
			// Create error shader as fallback
			m_shader = std::make_shared<renderer::Shader>("ErrorShader");
			m_shader->LoadErrorShader();
		}
	} else {
		TOAST_WARN("Material has no shader path, using error shader");
		m_shader = std::make_shared<renderer::Shader>("ErrorShader");
		m_shader->LoadErrorShader();
	}
}

void Material::Use() const {
	PROFILE_ZONE;
	if (!m_shader) {
		return;
	}

	// Activate shader first
	m_shader->Use();

	// Texture binding and sampler uniforms: keep a running texture unit index
	int tex_unit = 0;
	size_t tex_resource_index = 0;

	for (size_t i = 0; i < m_shaderParameters.size() && i < m_parameters.size(); ++i) {
		const auto& param_type = m_shaderParameters[i].type;
		const auto& param_name = m_shaderParameters[i].name;
		const auto& value_any = m_parameters[i].second;

		if (param_type == "texture") {
			// Ensure corresponding texture resource exists
			// m_textures was filled in the same order as parameters with string type
			if (tex_resource_index < m_textures.size() && m_textures[tex_resource_index]) {
				// Bind the texture to the unit and set sampler uniform
				m_textures[tex_resource_index]->Bind(tex_unit);
				m_shader->SetSampler(param_name, tex_unit);
			} else {
				// No texture loaded -> bind 0 (default)
				// It's optional to bind 0 here; leaving sampler to default if desired
				m_shader->SetSampler(param_name, tex_unit);
			}
			++tex_unit;
			++tex_resource_index;
		} else if (param_type == "vec4") {
			if (value_any.type() == typeid(glm::vec4)) {
				m_shader->Set(param_name, std::any_cast<glm::vec4>(value_any));
			}
		} else if (param_type == "vec3") {
			if (value_any.type() == typeid(glm::vec3)) {
				m_shader->Set(param_name, std::any_cast<glm::vec3>(value_any));
			}
		} else if (param_type == "float") {
			if (value_any.type() == typeid(float)) {
				m_shader->Set(param_name, std::any_cast<float>(value_any));
			}
		} else if (param_type == "mat4") {
			if (value_any.type() == typeid(glm::mat4)) {
				m_shader->Set(param_name, std::any_cast<glm::mat4>(value_any));
			}
		} else if (param_type == "color") {
			if (value_any.type() == typeid(glm::vec4)) {
				m_shader->Set(param_name, std::any_cast<glm::vec4>(value_any));
			}
		}
	}
	
	m_shader->Set("time", static_cast<float>(Time::uptime()));
}

void Material::UpdateEditorSlots() {
#ifdef TOAST_EDITOR
	// Determine how many texture slots we need (one per string parameter)
	std::vector<size_t> textureParamIndices;
	for (size_t idx = 0; idx < m_parameters.size(); ++idx) {
		if (m_parameters[idx].second.type() == typeid(std::string)) {
			textureParamIndices.push_back(idx);
		}
	}

	const size_t required = textureParamIndices.size();

	// Resize m_textureSlots to match required but reuse existing slots when possible
	// Update existing slots in-place to avoid deleting a slot while its callback is running.
	for (size_t s = 0; s < required; ++s) {
		const size_t paramIndex = textureParamIndices[s];

		// Safely extract initial path
		std::string initial;
		if (m_parameters[paramIndex].second.type() == typeid(std::string)) {
			try {
				initial = std::any_cast<std::string>(m_parameters[paramIndex].second);
			} catch (const std::bad_any_cast&) { initial = std::string(); }
		}

		if (initial.empty()) {
			initial = "images/default.png";    // default texture fallback
		}

		if (s < m_textureSlots.size() && m_textureSlots[s]) {
			// Reuse existing slot: update initial resource and callback
			editor::ResourceSlot* slot = m_textureSlots[s];
			// Clear old callback so SetInitialResource doesn't call it
			slot->SetOnDroppedLambda(std::function<void(const std::string&)>());
			// Set initial resource first so ProcessDrop doesn't invoke our callback while we're still setting up
			slot->SetInitialResource(initial);
			slot->SetOnDroppedLambda([this, index = paramIndex](const std::string& p) {
				// Update texture path in the material parameter
				m_parameters[index].second = p;
				// Persist and reload resources; do NOT call UpdateEditorSlots() from here
				SaveMaterial();
				LoadResources();
			});
		} else {
			// Create a new slot and append
			auto* slot = new editor::ResourceSlot(resource::ResourceType::TEXTURE, initial);
			// Load the initial resource first (this will not call our lambda yet because it's not set)
			slot->SetInitialResource(initial);
			slot->SetOnDroppedLambda([this, index = paramIndex](const std::string& p) {
				m_parameters[index].second = p;
				SaveMaterial();
				LoadResources();
			});
			m_textureSlots.push_back(slot);
		}
	}

	// If there are extra slots from a previous state, delete them now (safe because we didn't call this from within a slot callback)
	if (m_textureSlots.size() > required) {
		for (size_t s = required; s < m_textureSlots.size(); ++s) {
			delete m_textureSlots[s];
		}
		m_textureSlots.erase(m_textureSlots.begin() + required, m_textureSlots.end());
	}

	// Shader slot: create or update
	std::string shader_initial;
	if (!m_shaderPath.empty()) {
		shader_initial = m_shaderPath;
	}

	if (!m_shaderSlot) {
		m_shaderSlot = new editor::ResourceSlot(resource::ResourceType::SHADER, shader_initial);
		// Set initial resource first
		m_shaderSlot->SetInitialResource(shader_initial);
		m_shaderSlot->SetOnDroppedLambda([this](const std::string& p) {
			// Defer actual reload to ShowEditor()
			m_shaderPath = p;
			SaveMaterial();
			m_pendingReloadShader = true;
		});
	} else {
		// Clear old callback to prevent it firing during SetInitialResource
		m_shaderSlot->SetOnDroppedLambda(std::function<void(const std::string&)>());
		m_shaderSlot->SetInitialResource(shader_initial);
		m_shaderSlot->SetOnDroppedLambda([this](const std::string& p) {
			m_shaderPath = p;
			SaveMaterial();
			m_pendingReloadShader = true;
		});
	}
#endif
}

void Material::LoadErrorMaterial() {
	PROFILE_ZONE;
	TOAST_WARN("Loading error material");
	
	// Clear existing data
	m_shaderParameters.clear();
	m_parameters.clear();
	m_textures.clear();
	
	m_shader = std::make_shared<renderer::Shader>("ErrorShader");
	// m_shader->LoadErrorShader();
	
	// Set material as loaded
	SetResourceState(resource::ResourceState::UPLOADEDGPU);
}

}    // namespace renderer
