/**
 * @file Material.hpp
 * @author dario
 * @date 05/11/2025
 * @brief Material resource for combining shaders and textures.
 *
 * This file provides the Material class which bundles a shader with
 * textures and parameters for rendering objects.
 */

#pragma once
#include "Shader.hpp"
#include "Toast/Resources/Texture.hpp"

#include <any>
#include <memory>
#include <string>
#include <vector>

// Forward declare editor types to avoid pulling heavy editor headers into this public header
namespace editor {
class ResourceSlot;
}

namespace renderer {

/**
 * @class Material
 * @brief Resource that combines a shader with textures and parameters.
 *
 * Materials define how objects are rendered by bundling together:
 * - A shader program
 * - Textures (diffuse, normal, etc.)
 * - Shader parameters (colors, floats, matrices)
 *
 * Materials are loaded from JSON files that reference a shader and define
 * parameter values.
 *
 * @par Material File Format:
 * @code{.json}
 * {
 *     "shaderPath": "shaders/standard.shader",
 *     "materialParams": {
 *         "albedoTexture": "textures/brick.png",
 *         "normalTexture": "textures/brick_normal.png",
 *         "tintColor": [1.0, 0.8, 0.6, 1.0]
 *     }
 * }
 * @endcode
 *
 * @par Usage Example:
 * @code
 * // Load a material
 * auto material = resourceManager->LoadResource<Material>("materials/brick.mat");
 *
 * // Use in rendering
 * material->Use();  // Binds shader and textures
 * mesh->Draw();
 * @endcode
 *
 * @note Materials are shared resources - changes affect all users.
 * @warning Textures may be garbage collected if not properly referenced.
 *
 * @see Shader, Texture, IResource
 */
class Material : public IResource {
public:
	/**
	 * @struct ShaderParameter
	 * @brief Describes a parameter defined in the shader.
	 */
	struct ShaderParameter {
		std::string name;            ///< Parameter name in the shader.
		std::string type;            ///< Parameter type (texture, float, vec3, etc.).
		std::string defaultValue;    ///< Default value when creating new materials.
	};

	/**
	 * @brief Constructs a material from a file path.
	 * @param path Path to the material JSON file.
	 */
	explicit Material(std::string_view path);

	~Material() override;

	/**
	 * @brief Loads the material data (can be called from background thread).
	 */
	void Load() override;

	/**
	 * @brief Loads GPU resources (must be called from main thread).
	 */
	void LoadMainThread() override;

	/**
	 * @brief Shows the material editor UI (editor only).
	 */
	void ShowEditor();

	/**
	 * @brief Loads/reloads the material from its JSON file.
	 */
	void LoadMaterial();

	/**
	 * @brief Saves the current material state to its JSON file.
	 */
	void SaveMaterial();

	/**
	 * @brief Loads the shader and textures from paths.
	 *
	 * Also binds shader uniforms and texture slots.
	 */
	void LoadResources();

	/**
	 * @brief Binds the shader and textures for rendering.
	 *
	 * Call this before drawing meshes that use this material.
	 */
	void Use() const;

	/**
	 * @brief Gets the shader used by this material.
	 * @return Shared pointer to the shader.
	 */
	std::shared_ptr<renderer::Shader> GetShader() {
		return m_shader;
	}

private:
	/**
	 * @brief Updates editor UI slots when material is reloaded.
	 */
	void UpdateEditorSlots();

	/// @brief Path to the material JSON file.
	std::string m_materialPath;

	/// @brief Parameters defined in the shader.
	std::vector<ShaderParameter> m_shaderParameters;

	/// @brief Path to the shader file.
	std::string m_shaderPath;

	/// @brief Current parameter values (name -> value).
	std::vector<std::pair<std::string, std::any>> m_parameters;

	/// @brief Loaded textures referenced by the material.
	std::vector<std::shared_ptr<Texture>> m_textures;

	/// @brief The shader used for rendering.
	std::shared_ptr<renderer::Shader> m_shader;

	/// @brief Editor UI slots for texture selection.
	std::vector<editor::ResourceSlot*> m_textureSlots;

	/// @brief Editor UI slot for shader selection.
	editor::ResourceSlot* m_shaderSlot = nullptr;

	/// @brief Flag to trigger material reload after shader change in editor.
	bool m_pendingReloadShader = false;
};

}
