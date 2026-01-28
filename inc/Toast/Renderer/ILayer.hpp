/**
 * @file ILayer.hpp
 * @author Dario
 * @date 15/09/25
 * @brief Base interface for rendering layers.
 *
 * This file provides the ILayer interface which defines the contract
 * for render layers that can be pushed onto the LayerStack.
 */

#pragma once
#include "Toast/Event/Event.hpp"

namespace renderer {

/**
 * @class ILayer
 * @brief Abstract base class for render layers.
 *
 * Layers provide a way to organize rendering into separate passes.
 * Each layer has its own tick and render callbacks, and layers are
 * processed in the order they were added to the LayerStack.
 *
 * @par Layer Types:
 * - **Regular Layers**: Rendered before overlays (game content)
 * - **Overlays**: Rendered last (UI, debug visualization)
 *
 * @par Creating a Custom Layer:
 * @code
 * class UILayer : public ILayer {
 * public:
 *     UILayer() : ILayer("UILayer") {}
 *
 *     void OnAttach() override {
 *         // Initialize resources
 *     }
 *
 *     void OnDetach() override {
 *         // Cleanup resources
 *     }
 *
 *     void OnTick() override {
 *         // Update UI state
 *     }
 *
 *     void OnRender() override {
 *         // Render UI elements
 *     }
 * };
 * @endcode
 *
 * @see LayerStack
 */
class ILayer {
public:
	/**
	 * @brief Constructs a layer with the given name.
	 * @param name Debug name for the layer.
	 */
	ILayer(std::string name = "Default Layer") : m_name(std::move(name)) { }

	virtual ~ILayer() = default;

	/**
	 * @brief Called when the layer is added to the stack.
	 *
	 * Use this to initialize resources, subscribe to events, etc.
	 */
	virtual void OnAttach() = 0;

	/**
	 * @brief Called when the layer is removed from the stack.
	 *
	 * Use this to clean up resources, unsubscribe from events, etc.
	 */
	virtual void OnDetach() = 0;

	/**
	 * @brief Called every frame for layer updates.
	 *
	 * Use this for per-frame logic that doesn't involve rendering.
	 */
	virtual void OnTick() = 0;

	/**
	 * @brief Called every frame to render the layer.
	 *
	 * Use this to issue draw calls and render layer content.
	 */
	virtual void OnRender() = 0;

private:
	/// @brief Debug name for the layer.
	std::string m_name;
};

}
