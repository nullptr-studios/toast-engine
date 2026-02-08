/// @name engine.hpp
/// @author Xein
/// @date 10 Feb 2026

namespace toast {

struct EnginePimpl;

class Engine final {
public:
	Engine() noexcept;
	~Engine() noexcept;
	static Engine* get() noexcept;

	Engine(Engine&) = delete;
	Engine(Engine&&) = delete;
	Engine& operator=(const Engine&) = delete;
	Engine& operator=(const Engine&&) = delete;

	void tick();
	bool shouldClose();
	void test();


private:
	EnginePimpl* m;
	static Engine* instance;

};

}