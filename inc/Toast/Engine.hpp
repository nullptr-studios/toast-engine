/// @file   Engine.hpp
/// @author Xein
/// @date   15/03/25
/// @brief  Main engine class

#pragma once

namespace toast {

class Engine {
public:
	Engine();                           ///< @brief creates the engine if it doesn´t exist yet
	static Engine* get();               ///< @brief gets the current instance of the class
	void Run(int argc, char** argv);    ///< @note Call this to start the engine on the main function

	[[nodiscard]]
	bool GetShouldClose() const;          ///< @brief Returns if the engine should close or not
	static void ForcePurgeResources();    ///< Purges unused resources from memory

protected:
	virtual void Begin() { }      ///< Called on initialization

	virtual void EditorTick();    ///< Called every frame from the editor
	virtual void Render();        ///< Called every frame for rendering
	virtual void Close();         ///< Called when the engine is closing

	struct Pimpl;
	Pimpl* m;

	/// This vector contains all the runtime arguments, very useful for tests or for some CLI tools
	std::vector<std::string> m_arguments;
	std::atomic<bool> m_windowShouldClose = false;

private:
	void Init();
	static Engine* m_instance;
	static double purge_timer;
};

}
