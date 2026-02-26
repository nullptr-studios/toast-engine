#include "Toast/Log.hpp"
#include "Toast/Renderer/HUD/HUDLayer.hpp"
#include "Toast/Renderer/LayerStack.hpp"
#include "Toast/Resources/ToastFileSystem.hpp"
#include "Toast/Ui/FontHandler.hpp"
#include "Toast/Ui/Logger.hpp"

#include <Toast/Ui/Manager.hpp>

namespace ui {

UiSystem::UiSystem(toast::Window& window, bool msaa) {
	auto size = window.GetFramebufferSize();
	auto* ui = new renderer::HUD::HUDLayer(window.GetWindow(), size.x, size.y, msaa);

	Configure();

	// m is constructed here
	m = {
		.layer = ui,
	};

	renderer::LayerStack::GetInstance()->PushOverlay(ui);
	ui->LoadURL("file:///assets/UI/hud.html");
}

void UiSystem::Configure() {
	ultralight::Config config;

	// The resources folder should contain: cacert.pem, icudt67l.dat
	config.resource_path_prefix = "UI/Ultralight/resources/";
	config.face_winding = ultralight::FaceWinding::CounterClockwise;
	config.force_repaint = false;    // Required for stable rendering with custom GPU driver
	config.animation_timer_delay = 1.0 / 60.0;
	config.scroll_timer_delay = 1.0 / 60.0;
	config.recycle_delay = 4.0;
	config.memory_cache_size = 64 * 1024 * 1024;    // 64MB
	config.page_cache_size = 0;
	config.override_ram_size = 0;
	config.min_large_heap_size = 32 * 1024 * 1024;    // 32MB
	config.min_small_heap_size = 1 * 1024 * 1024;     // 1MB
	config.num_renderer_threads = 2;                  // Use main thread
	                                                  //
	ultralight::Platform::instance().set_config(config);
	ultralight::Platform::instance().set_file_system(&ToastFileSystem::Get());
	ultralight::Platform::instance().set_logger(&ui::UiLogger::get());
	ultralight::Platform::instance().set_font_loader(&ui::UiFontLoader::get());

	TOAST_INFO("Ultralight platform initialized");
	TOAST_TRACE("Resource path: UI/Ultralight/resources/");
	TOAST_TRACE("Make sure icudt67l.dat and cacert.pem exist in the resources folder!");
}

}
