/// @file Material.hpp
/// @author dario
/// @date 05/11/2025.

#pragma once
#include "Toast/Resources/Texture.hpp"
#include "Shader.hpp"

#include <any>
#include <memory>
#include <string>
#include <vector>

// Forward declare editor types to avoid pulling heavy editor headers into this public header
namespace editor {
class ResourceSlot;
}

namespace renderer {

///@brief Material class that holds shader and textures
///@note all materials are shared, aka u change something and all entities using it will have the change
//@TODO: theres a weird bug, sometimes the texture gets GC???????
///
class Material : public IResource {
public:
	struct ShaderParameter {
		std::string name;
		std::string type;
		std::string defaultValue;    // Used when creating new materials
	};

	explicit Material(std::string_view path);
	~Material() override;

	void Load() override;
	void LoadMainThread() override;

	// imgui stuff
	void ShowEditor();

	// loads the json into memory
	void LoadMaterial();
	void SaveMaterial();

	// load shader and textures from paths
	// also binds uniforms and texture slots
	void LoadResources();

	// bind shader and textures
	void Use() const;

	std::shared_ptr<renderer::Shader> GetShader() {
		return m_shader;
	}

private:
	void UpdateEditorSlots();

	std::string m_materialPath;

	std::vector<ShaderParameter> m_shaderParameters;

	std::string m_shaderPath;

	std::vector<std::pair<std::string, std::any>> m_parameters;

	std::vector<std::shared_ptr<Texture>> m_textures;

	std::shared_ptr<renderer::Shader> m_shader;

	std::vector<editor::ResourceSlot*> m_textureSlots;
	editor::ResourceSlot* m_shaderSlot = nullptr;
	// When a shader is changed via the editor drop, we set this flag to true in the drop callback
	// and perform the actual material reload on the next ShowEditor()
	bool m_pendingReloadShader = false;
};
}
