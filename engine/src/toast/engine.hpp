/// @name engine.hpp
/// @author Xein
/// @date 10 Feb 2026

#include <cstdint>
#include <toast/uid.hpp>

namespace toast {
namespace renderer {
class VulkanCore;
class VulkanRenderer;
class SharedTextureOutputTarget;
struct ViewportFrameDesc;
}

struct EnginePimpl;

class Engine final {
public:
	Engine() noexcept;
	~Engine() noexcept;
	static auto get() noexcept -> Engine*;

	Engine(Engine&) = delete;
	Engine(Engine&&) = delete;
	auto operator=(const Engine&) -> Engine& = delete;
	auto operator=(const Engine&&) -> Engine& = delete;

	void init();
	void tick();
	auto shouldClose() -> bool;
	void test();

	void reloadSettings();

	// window
	void createSDLWindow(const char*);
	void createAvaloniaWindow();

	// nodes
	auto createWorkspace(std::string_view type) -> std::pair<UID, std::string>;
	auto openWorkspace(UID uid) -> std::pair<UID, std::string>;
	/// @brief autosave recovery
	auto openWorkspace(UID uid, std::string_view source_uri) -> std::pair<UID, std::string>;
	/// @brief Clones the given workspace's into a new ticking PlayWorkspace
	auto playWorkspace(UID source_handle) -> std::pair<UID, std::string>;
	void destroyWorkspace(UID handle);

	auto activeWorkspace() -> UID;

	/// @brief Re-resolves NodeInfo after a project reload
	void refreshNodeInfos();

	/// @brief Copies the latest viewport frame into @p dst
	/// @return 1 copied, 0 none available, -1 destination too small
	auto getViewportFrame(void* dst, uint32_t dst_capacity, renderer::ViewportFrameDesc* out) -> int;

	/// @brief Calls begin() on the currently registered application layer (if any)
	void beginApplication();

	/// @brief Destroys the active application layer and nulls the pointer
	void popApplication();

	/// @brief Creates the World, then loads and activates the init_scene from project settings
	/// @note Must be called after toast_init() and toast_create_*_window()
	void startGame();

private:
	EnginePimpl* m;
	static Engine* instance;
};

/*
 *	The order to call and initialize the engine on the Editor and the Player should be:
 *		- Create an engine -> toast_create()
 *		- Create a game -> game_create()
 *		- Set the working directories -> toast_set_working_directory()
 *		- Call Init() -> toast_init()
 *		- Create window -> toast_create_[...]_window()
 */

}
