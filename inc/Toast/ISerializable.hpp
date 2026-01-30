/**
 * @file ISerializable.hpp
 * @author Xein <xgonip@gmail.com>
 * @date 9/30/25
 * @brief Interface for objects that can be saved and loaded from JSON.
 *
 * This file defines the ISerializable interface which provides a contract
 * for saving and loading object state to/from JSON format.
 */

#pragma once
#include <nlohmann/json.hpp>

/// @brief Type alias for ordered JSON objects.
using json_t = nlohmann::ordered_json;

namespace toast {

/**
 * @class ISerializable
 * @brief Interface for objects that support JSON serialization.
 *
 * ISerializable defines the contract for objects that can save their
 * state to JSON and restore it from JSON. This is used for scene
 * files, prefabs, and runtime state persistence.
 *
 * @par Implementing Serialization:
 * @code
 * class MyActor : public Actor {
 * public:
 *     json_t Save() const override {
 *         auto j = Actor::Save();  // Call parent
 *         j["health"] = m_health;
 *         j["score"] = m_score;
 *         return j;
 *     }
 *
 *     void Load(json_t j, bool force_create) override {
 *         Actor::Load(j, force_create);  // Call parent
 *         m_health = j.value("health", 100.0f);
 *         m_score = j.value("score", 0);
 *     }
 *
 * private:
 *     float m_health = 100.0f;
 *     int m_score = 0;
 * };
 * @endcode
 *
 * @par Soft Save/Load:
 * Soft serialization uses cached JSON for quick state restoration
 * without re-parsing files. Used for editor undo/redo and play mode.
 *
 * @see Object, Scene, json_t
 */
class ISerializable {
public:
	/**
	 * @brief Loads object state from JSON.
	 *
	 * Deserializes the object's properties from the provided JSON.
	 *
	 * @param j JSON object containing serialized state.
	 * @param force_create If true, always creates new children; if false,
	 *                     updates existing children by name.
	 */
	virtual void Load(json_t j, bool force_create = true) = 0;

	/**
	 * @brief Reloads from cached JSON.
	 *
	 * Restores the object to its last saved state without reading
	 * from disk. Used for quick state restoration.
	 */
	virtual void SoftLoad() = 0;

	/**
	 * @brief Saves object state to JSON.
	 *
	 * Serializes the object's properties to a JSON object.
	 *
	 * @return JSON object containing the serialized state.
	 */
	[[nodiscard]]
	virtual json_t Save() const = 0;

	/**
	 * @brief Caches current state for quick restoration.
	 *
	 * Saves the current state to an internal cache. Used before
	 * entering play mode in the editor.
	 */
	virtual void SoftSave() const = 0;

#ifdef TOAST_EDITOR
	/**
	 * @brief Renders the inspector UI for this object.
	 *
	 * Called by the editor to display property editors for this object.
	 *
	 * @note Only available in editor builds.
	 */
	virtual void Inspector() = 0;
#endif
};

}
