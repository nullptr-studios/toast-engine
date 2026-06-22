/// @name engine.hpp
/// @author Xein
/// @date 10 Feb 2026

namespace toast {
namespace renderer {
class VulkanCore;
class VulkanRenderer;
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

	void tick();
	auto shouldClose() -> bool;
	void test();

	// window
	void createSDLWindow(const char*);
	void createAvaloniaWindow();

private:
	EnginePimpl* m;
	static Engine* instance;
};

}
