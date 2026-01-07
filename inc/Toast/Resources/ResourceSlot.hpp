/// @file ResourceSlot.hpp
/// @author dario
/// @date 27/10/2025.

#pragma once
#include "Toast/Resources/IResource.hpp"
#include "Texture.hpp"

#include <memory>

namespace editor {

class ResourceSlot final {
public:
	struct Entry {
		std::filesystem::path relativePath;    // path relative to assets/
		std::string name;                      // filename
		std::string extension;                 // lowercase extension with dot
		bool isDirectory = false;
		std::shared_ptr<Texture> icon;         // icon to display
	};

	///@param required_type the required resource type for this slot
	///@param default_path optional default path to load initially shoud be seted the one parsed from the json
	ResourceSlot(resource::ResourceType required_type, std::string default_path = "");

#ifdef TOAST_EDITOR

	///@brief Sets the initial resource to load when showing the slot for the first time
	void SetInitialResource(const std::string& default_path);

	///@brief editor callback when a resource is dropped
	void SetOnDroppedLambda(std::function<void(const std::string& path)> func) {
		m_onDropped = std::move(func);
	}

	///@brief Changes the resource if something has changed
	/// Call this in setters to update the slot
	void SetResource(const std::string& path);

	void Show();
#endif

	void name(const std::string& name) {
		m_name = name;
	}

	///@brief this returns the currently selected resource (can be empty!!)
	/// Yes its a string path bc thats how resources are identified/loaded/stored
	/// the actual resource maneagement and correct type handling is left to the user of this class
	std::string GetResourcePath() const {
		return ToForwardSlashes(m_selectedEntry.relativePath.string());
	}

private:
	[[nodiscard]]
	std::string ToForwardSlashes(const std::string& s) const {
		std::string result = s;
		std::ranges::replace(result, '\\', '/');
		return result;
	}

#ifdef TOAST_EDITOR
	bool CheckCorrectType(Entry* res) const;

	void RenderThumbnailArea();
	void RenderDetailsArea();

	void ProcessDrop(Entry* e);

	// Popup state for showing errors to the user
	void RenderPopups();
	bool m_showTypeErrorPopup = false;
	std::string m_typeErrorMessage;
#endif

	std::string m_name = "Resource slot";
	std::string m_defaultPath;
	Entry m_selectedEntry;
	resource::ResourceType m_requiredType;

	std::function<void(const std::string& path)> m_onDropped;
};

}
