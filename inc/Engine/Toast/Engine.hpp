/// @file   Engine.hpp
/// @author Xein
/// @date   15/03/25
/// @brief  Main engine class

#pragma once

#include "Engine/Resources/ResourceManager.hpp"

#include <Engine/Core/Time.hpp>
#include <Engine/Event/EventSystem.hpp>
#include <Engine/Renderer/IRendererBase.hpp>
#include <Engine/Renderer/LayerStack.hpp>
#include <Engine/Toast/Factory.hpp>
#include <Engine/Toast/ProjectSettings.hpp>
#include <Engine/Toast/World.hpp>
#include <Engine/Window/Window.hpp>

namespace input {
class InputSystem;
}

namespace physics {
class PhysicsSystem;
}

namespace toast {

class Engine {
public:
	void Run(int argc, char** argv);    ///< @note Call this to start the engine on the main function

	static void ForcePurgeResources() {
		purge_timer = UINT8_MAX;
	}    ///< Purges unused resources from memory

	bool GetShouldClose() const;     /// @brief Returns if the engine should close or not
	static Engine* GetInstance();    /// @brief gets the current instance of the class
	Engine();                        /// @brief creates the engine if it doesn´t exist yet

private:
	/// This function is used for initializing the engine. It is called before the game loop starts.
	///
	/// It is a private function that should not be called directly. It is called by the Run function.
	/// Adding functionality on the init is done on the begin function. It is separated like this
	/// because you will want to add functionality in between parts of the Init function, not directly
	/// at the end. For that, the Begin funciton is provided. It will be called during the engine
	/// initialization.
	void Init();

	static Engine* m_instance;
	static float purge_timer;

protected:
	virtual void Begin() { }      ///< Called on initialization

	virtual void EditorTick();    ///< Called every frame from the editor

	virtual void Render();        ///< Called every frame for rendering
	virtual void Close();         ///< Called when the engine is closing

	/// This vector contains all the runtime arguments, very useful for tests or for some CLI tools
	std::vector<std::string> m_arguments;

	std::unique_ptr<Time> m_time;

	std::unique_ptr<event::EventSystem> m_eventSystem;

	std::unique_ptr<Window> m_window;
	std::atomic<bool> m_windowShouldClose = false;

	input::InputSystem* m_inputSystem;

	std::unique_ptr<World> m_gameWorld;

	std::unique_ptr<renderer::IRendererBase> m_renderer;

	std::unique_ptr<renderer::LayerStack> m_layerStack;

	std::unique_ptr<Factory> m_factory;

	std::unique_ptr<resource::ResourceManager> m_resourceManager;

	std::unique_ptr<ProjectSettings> m_projectSettings;

	physics::PhysicsSystem* m_physicsSystem;
};

}
