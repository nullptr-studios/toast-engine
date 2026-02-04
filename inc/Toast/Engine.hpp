/// @file   Engine.hpp
/// @author Xein
/// @date   15/03/25
/// @brief  Main engine class that manages the application lifecycle

#pragma once

namespace toast {

/**
 * @class Engine
 * @brief Core engine class that manages the main application loop and subsystems.
 *
 * The Engine class serves as the central hub for the Toast game engine. It initializes
 * all subsystems (rendering, input, physics, audio, etc.), manages the main game loop,
 * and handles the application lifecycle.
 *
 * @note This class follows the singleton pattern - only one instance can exist at a time.
 *
 * @par Usage Example:
 * @code
 * class MyGame : public toast::Engine {
 * protected:
 *     void Begin() override {
 *         // Initialize your game here
 *     }
 *     void Render() override {
 *         // Custom rendering logic
 *     }
 * };
 *
 * int main(int argc, char** argv) {
 *     MyGame game;
 *     game.Run(argc, argv);
 *     return 0;
 * }
 * @endcode
 *
 * @see World, Time, Window
 */
class Engine {
public:
	/**
	 * @brief Constructs the engine instance.
	 * @throws ToastException if an engine instance already exists.
	 */
	Engine();

	/**
	 * @brief Gets the current engine instance.
	 * @return Pointer to the singleton engine instance, or nullptr if not created.
	 */
	static Engine* get();

	/**
	 * @brief Starts the engine and enters the main game loop.
	 *
	 * This method initializes all subsystems and begins the main loop which
	 * processes events, updates game logic, and renders frames until the
	 * application is closed.
	 *
	 * @param argc Number of command-line arguments.
	 * @param argv Array of command-line argument strings.
	 *
	 * @note This method blocks until the application exits.
	 * @warning Must be called from the main thread.
	 */
	void Run(int argc, char** argv);

	static void SetRenderdocApi(void* api);

	static void* GetRenderdocApi();

	/**
	 * @brief Checks if the engine should close.
	 * @return true if the engine should terminate, false otherwise.
	 */
	[[nodiscard]]
	bool GetShouldClose() const;

	/**
	 * @brief Forces an immediate purge of unused cached resources.
	 *
	 * Normally, resources are automatically purged every 120 seconds.
	 * Call this method to trigger an immediate cleanup of resources
	 * that are no longer referenced.
	 *
	 * @see resource::ResourceManager::PurgeResources()
	 */
	static void ForcePurgeResources();

protected:
	/**
	 * @brief Called once during engine initialization.
	 *
	 * Override this method to perform custom initialization logic
	 * such as loading the initial scene or setting up game state.
	 *
	 * @note Called after all subsystems are initialized but before
	 *       the main loop begins.
	 */
	virtual void Begin() { }

	/**
	 * @brief Called every frame in editor mode.
	 *
	 * Override this method to add custom editor-specific update logic.
	 * This is called in addition to the normal game tick when running
	 * in the editor.
	 */
	virtual void EditorTick();

	/**
	 * @brief Called every frame for rendering.
	 *
	 * Override this method to add custom rendering logic that should
	 * execute after the main renderer has finished.
	 */
	virtual void Render();

	/**
	 * @brief Called when the engine is shutting down.
	 *
	 * Override this method to perform cleanup operations such as
	 * saving game state or releasing custom resources.
	 *
	 * @note Called before subsystems are destroyed.
	 */
	virtual void Close();

	/// @brief Forward declaration of the private implementation structure.
	struct Pimpl;

	/// @brief Pointer to the private implementation (PIMPL idiom).
	Pimpl* m;

	/**
	 * @brief Command-line arguments passed to Run().
	 *
	 * Contains all runtime arguments, useful for tests, CLI tools,
	 * or conditional initialization based on launch parameters.
	 */
	std::vector<std::string> m_arguments;

	/// @brief Atomic flag indicating if the window should close.
	std::atomic<bool> m_windowShouldClose = false;

private:
	/**
	 * @brief Initializes all engine subsystems.
	 * @note Called internally by Run().
	 */
	void Init();

	/// @brief Singleton instance pointer.
	static Engine* m_instance;

	/// @brief Timer for automatic resource purging (uses monotonic uptime).
	static double purge_timer;

	static void* renderdoc_api;
};

}
