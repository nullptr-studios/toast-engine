/// @name engine.hpp
/// @author Xein
/// @date 10 Feb 2026

#include <cstdint>

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
	~Engine() noexcept = default;
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

	/// @brief Copies the latest viewport frame into @p dst
	/// @return 1 copied, 0 none available, -1 destination too small
	int getViewportFrame(void* dst, uint32_t dstCapacity, renderer::ViewportFrameDesc* out);

private:
	EnginePimpl* m;
	static Engine* instance;
};

}
