/// @name engine.hpp
/// @author Xein
/// @date 10 Feb 2026

#include <cstdint>
#include <toast/uid.hpp>

namespace toast {
class MeshNode;

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

	// window
	void createSDLWindow(const char*);
	void createAvaloniaWindow();

	// nodes
	auto createWorkspace(std::string_view type) -> std::pair<UID, std::string>;
	auto openWorkspace(UID uid) -> std::pair<UID, std::string>;
	void destroyWorkspace(UID handle);

	void registerMeshNodeProxy(MeshNode* node);
	void unregisterMeshNodeProxy(MeshNode* node);

	auto activeWorkspace() -> UID;

	/// @brief Copies the latest viewport frame into @p dst
	/// @return 1 copied, 0 none available, -1 destination too small
	auto getViewportFrame(void* dst, uint32_t dst_capacity, renderer::ViewportFrameDesc* out) -> int;

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
