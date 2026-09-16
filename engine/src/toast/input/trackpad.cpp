#include "trackpad_math.hpp"

#include <trackpad.h>

#ifdef _WIN32

#include <Windows.h>
#include <atomic>
#include <commctrl.h>
#include <cstdint>
#include <directmanipulation.h>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <wrl/client.h>

#ifndef DM_POINTERHITTEST
#define DM_POINTERHITTEST 0x0250
#endif

namespace {

using Microsoft::WRL::ComPtr;

constexpr LONG k_fake_viewport = 10000;
constexpr UINT_PTR k_subclass_id = 0x544F415354545044ull;

struct ViewportState;

class ViewportHandler final : public IDirectManipulationViewportEventHandler {
public:
	explicit ViewportHandler(ViewportState* owner) : m_owner(owner) { }

	auto STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) -> HRESULT override;

	auto STDMETHODCALLTYPE AddRef() -> ULONG override { return ++m_refs; }

	auto STDMETHODCALLTYPE Release() -> ULONG override {
		const auto refs = --m_refs;
		if (refs == 0) {
			delete this;
		}
		return refs;
	}

	auto STDMETHODCALLTYPE OnViewportStatusChanged(
	    IDirectManipulationViewport* viewport, DIRECTMANIPULATION_STATUS current, DIRECTMANIPULATION_STATUS previous
	) -> HRESULT override;

	auto STDMETHODCALLTYPE OnViewportUpdated(IDirectManipulationViewport*) -> HRESULT override { return S_OK; }

	auto STDMETHODCALLTYPE OnContentUpdated(IDirectManipulationViewport*, IDirectManipulationContent* content) -> HRESULT override;

private:
	std::atomic<ULONG> m_refs {1};
	ViewportState* m_owner;
};

struct ViewportState {
	ComPtr<IDirectManipulationViewport> viewport;
	ComPtr<IDirectManipulationUpdateManager> update_manager;
	ComPtr<ViewportHandler> handler;
	DWORD handler_cookie = 0;
	bool inverted = false;

	RECT hit_rect {0, 0, 0, 0};

	std::mutex mutex;
	toast::input::TrackpadGesture gesture;
	toast::input::TrackpadDelta accumulated {0.0f, 0.0f, 0.0f};

	std::atomic<DIRECTMANIPULATION_STATUS> status {DIRECTMANIPULATION_BUILDING};

	void resetGesture(IDirectManipulationViewport* v) {
		std::scoped_lock lock(mutex);
		if (gesture.started()) {
			v->ZoomToRect(0.0f, 0.0f, static_cast<float>(k_fake_viewport), static_cast<float>(k_fake_viewport), FALSE);
		}
		gesture.reset();
	}

	void acceptTransform(IDirectManipulationContent* content) {
		float matrix[6] {};
		if (FAILED(content->GetContentTransform(matrix, ARRAYSIZE(matrix)))) {
			return;
		}

		const toast::input::TrackpadTransform current {
		  matrix[0] * toast::input::TrackpadGesture::pinch_scale,
		  matrix[4],
		  matrix[5],
		};

		std::scoped_lock lock(mutex);
		const auto delta = gesture.accept(current);
		accumulated.dx += delta.dx;
		accumulated.dy += delta.dy;
		accumulated.zoom += delta.zoom;
	}
};

auto ViewportHandler::QueryInterface(REFIID iid, void** object) -> HRESULT {
	if (!object) {
		return E_POINTER;
	}
	if (iid == __uuidof(IUnknown) || iid == __uuidof(IDirectManipulationViewportEventHandler)) {
		*object = static_cast<IDirectManipulationViewportEventHandler*>(this);
		AddRef();
		return S_OK;
	}
	*object = nullptr;
	return E_NOINTERFACE;
}

auto ViewportHandler::OnViewportStatusChanged(
    IDirectManipulationViewport* viewport, DIRECTMANIPULATION_STATUS current, DIRECTMANIPULATION_STATUS previous
) -> HRESULT {
	m_owner->status = current;
	if (current == previous) {
		return S_OK;
	}

	if (previous == DIRECTMANIPULATION_ENABLED || current == DIRECTMANIPULATION_READY ||
	    (previous == DIRECTMANIPULATION_INERTIA && current != DIRECTMANIPULATION_INERTIA)) {
		m_owner->resetGesture(viewport);
	}
	return S_OK;
}

auto ViewportHandler::OnContentUpdated(IDirectManipulationViewport*, IDirectManipulationContent* content) -> HRESULT {
	m_owner->acceptTransform(content);
	return S_OK;
}

struct WindowHost {
	HWND window = nullptr;
	ComPtr<IDirectManipulationManager> manager;
	ComPtr<IDirectManipulationUpdateManager> update_manager;
	std::unordered_map<uint64_t, std::shared_ptr<ViewportState>> viewports;
	bool com_initialized = false;
};

std::mutex g_mutex;
std::unordered_map<HWND, std::shared_ptr<WindowHost>> g_hosts;
std::unordered_map<uint64_t, std::pair<HWND, std::shared_ptr<ViewportState>>> g_viewports;
std::atomic_uint64_t g_next_handle = 1;

auto scrollInverted() -> bool {
	DWORD value = 1;
	DWORD size = sizeof(value);
	const auto status = RegGetValueW(
	    HKEY_CURRENT_USER,
	    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\PrecisionTouchPad\\",
	    L"ScrollDirection",
	    RRF_RT_REG_DWORD,
	    nullptr,
	    &value,
	    &size
	);
	return status == ERROR_SUCCESS && value == 0;
}

auto lookup(uint64_t handle) -> std::shared_ptr<ViewportState> {
	std::scoped_lock lock(g_mutex);
	auto it = g_viewports.find(handle);
	return it == g_viewports.end() ? nullptr : it->second.second;
}

void claimContact(HWND window, UINT32 pointer_id) {
	POINTER_INPUT_TYPE type {};
	if (!GetPointerType(pointer_id, &type) || type != PT_TOUCHPAD) {
		return;
	}

	POINTER_INFO info {};
	if (!GetPointerInfo(pointer_id, &info)) {
		return;
	}
	POINT point = info.ptPixelLocation;
	ScreenToClient(window, &point);

	std::shared_ptr<ViewportState> target;
	{
		std::scoped_lock lock(g_mutex);
		auto host_it = g_hosts.find(window);
		if (host_it == g_hosts.end()) {
			return;
		}
		for (const auto& [_, state] : host_it->second->viewports) {
			if (PtInRect(&state->hit_rect, point)) {
				target = state;
				break;
			}
		}
	}

	if (target) {
		target->viewport->SetContact(pointer_id);
	}
}

auto CALLBACK subclassProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR) -> LRESULT {
	if (message == DM_POINTERHITTEST) {
		claimContact(window, GET_POINTERID_WPARAM(wparam));
	}
	return DefSubclassProc(window, message, wparam, lparam);
}

auto createHost(HWND window) -> std::shared_ptr<WindowHost> {
	const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
		return {};
	}

	auto host = std::make_shared<WindowHost>();
	host->window = window;
	host->com_initialized = init == S_OK || init == S_FALSE;

	const auto fail = [&host]() -> std::shared_ptr<WindowHost> {
		if (host->com_initialized) {
			CoUninitialize();
		}
		return {};
	};

	if (FAILED(CoCreateInstance(
	        CLSID_DirectManipulationManager, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(host->manager.ReleaseAndGetAddressOf())
	    ))) {
		return fail();
	}
	if (FAILED(host->manager->GetUpdateManager(IID_PPV_ARGS(host->update_manager.ReleaseAndGetAddressOf()))) ||
	    FAILED(host->manager->Activate(window))) {
		return fail();
	}

	SetWindowSubclass(window, subclassProc, k_subclass_id, 0);
	return host;
}

}

extern "C" {

auto toast_trackpad_supported() noexcept -> int32_t {
	const auto module = LoadLibraryW(L"directmanipulation.dll");
	if (!module) {
		return 0;
	}
	FreeLibrary(module);
	return 1;
}

auto toast_trackpad_create(void* native_window) noexcept -> uint64_t {
	try {
		auto window = static_cast<HWND>(native_window);
		if (!window || !IsWindow(window)) {
			return 0;
		}

		std::scoped_lock lock(g_mutex);
		auto host_it = g_hosts.find(window);
		std::shared_ptr<WindowHost> host;
		if (host_it == g_hosts.end()) {
			host = createHost(window);
			if (!host) {
				return 0;
			}
			g_hosts.emplace(window, host);
		} else {
			host = host_it->second;
		}

		auto state = std::make_shared<ViewportState>();
		state->update_manager = host->update_manager;
		state->inverted = scrollInverted();
		if (FAILED(host->manager->CreateViewport(nullptr, window, IID_PPV_ARGS(state->viewport.ReleaseAndGetAddressOf())))) {
			return 0;
		}

		const auto config = static_cast<DIRECTMANIPULATION_CONFIGURATION>(
		    DIRECTMANIPULATION_CONFIGURATION_INTERACTION | DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_X |
		    DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_Y | DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_INERTIA |
		    DIRECTMANIPULATION_CONFIGURATION_SCALING
		);

		state->handler.Attach(new ViewportHandler(state.get()));
		const RECT rect {0, 0, k_fake_viewport, k_fake_viewport};
		if (FAILED(state->viewport->ActivateConfiguration(config)) ||
		    FAILED(state->viewport->SetViewportOptions(DIRECTMANIPULATION_VIEWPORT_OPTIONS_MANUALUPDATE)) ||
		    FAILED(state->viewport->AddEventHandler(window, state->handler.Get(), &state->handler_cookie)) ||
		    FAILED(state->viewport->SetViewportRect(&rect)) || FAILED(state->viewport->Enable())) {
			return 0;
		}

		const auto handle = g_next_handle++;
		host->viewports.emplace(handle, state);
		g_viewports.emplace(handle, std::make_pair(window, state));
		return handle;
	} catch (...) { return 0; }
}

void toast_trackpad_destroy(uint64_t handle) noexcept {
	try {
		std::shared_ptr<WindowHost> host;
		std::shared_ptr<ViewportState> state;
		HWND window = nullptr;
		bool host_empty = false;
		{
			std::scoped_lock lock(g_mutex);
			auto it = g_viewports.find(handle);
			if (it == g_viewports.end()) {
				return;
			}
			window = it->second.first;
			state = it->second.second;
			g_viewports.erase(it);

			auto host_it = g_hosts.find(window);
			if (host_it != g_hosts.end()) {
				host = host_it->second;
				host->viewports.erase(handle);
				host_empty = host->viewports.empty();
				if (host_empty) {
					g_hosts.erase(host_it);
				}
			}
		}

		if (state->viewport) {
			state->viewport->Stop();
			if (state->handler_cookie) {
				state->viewport->RemoveEventHandler(state->handler_cookie);
			}
			state->viewport->Abandon();
		}
		state->handler.Reset();
		state->viewport.Reset();
		state->update_manager.Reset();

		if (host && host_empty) {
			host->manager->Deactivate(window);
			RemoveWindowSubclass(window, subclassProc, k_subclass_id);
			host->update_manager.Reset();
			host->manager.Reset();
			if (host->com_initialized) {
				CoUninitialize();
			}
		}
	} catch (...) { }
}

void toast_trackpad_set_rect(uint64_t handle, int32_t x, int32_t y, int32_t width, int32_t height) noexcept {
	try {
		std::scoped_lock lock(g_mutex);
		auto it = g_viewports.find(handle);
		if (it == g_viewports.end()) {
			return;
		}
		it->second.second->hit_rect = RECT {x, y, x + width, y + height};
	} catch (...) { }
}

void toast_trackpad_update(uint64_t handle) noexcept {
	try {
		auto state = lookup(handle);
		if (!state || !state->update_manager) {
			return;
		}
		const auto status = state->status.load();
		if (status == DIRECTMANIPULATION_RUNNING || status == DIRECTMANIPULATION_INERTIA) {
			state->update_manager->Update(nullptr);
		}
	} catch (...) { }
}

auto toast_trackpad_drain(uint64_t handle, toast_trackpad_state* out_state) noexcept -> int32_t {
	if (!out_state) {
		return 0;
	}
	try {
		auto state = lookup(handle);
		if (!state) {
			return 0;
		}

		std::scoped_lock lock(state->mutex);
		if (state->accumulated.dx == 0.0f && state->accumulated.dy == 0.0f && state->accumulated.zoom == 0.0f) {
			return 0;
		}
		out_state->pan_x = state->accumulated.dx;
		out_state->pan_y = state->accumulated.dy;
		out_state->zoom = state->accumulated.zoom;
		out_state->inverted = state->inverted ? 1 : 0;
		state->accumulated = {0.0f, 0.0f, 0.0f};
		return 1;
	} catch (...) { return 0; }
}

auto toast_trackpad_active(uint64_t handle) noexcept -> int32_t {
	try {
		auto state = lookup(handle);
		if (!state) {
			return 0;
		}
		const auto status = state->status.load();
		return status == DIRECTMANIPULATION_RUNNING || status == DIRECTMANIPULATION_INERTIA ? 1 : 0;
	} catch (...) { return 0; }
}
}

#else

extern "C" {

auto toast_trackpad_supported() noexcept -> int32_t {
	return 0;
}

auto toast_trackpad_create(void*) noexcept -> uint64_t {
	return 0;
}

void toast_trackpad_destroy(uint64_t) noexcept { }

void toast_trackpad_set_rect(uint64_t, int32_t, int32_t, int32_t, int32_t) noexcept { }

void toast_trackpad_update(uint64_t) noexcept { }

auto toast_trackpad_drain(uint64_t, toast_trackpad_state*) noexcept -> int32_t {
	return 0;
}

auto toast_trackpad_active(uint64_t) noexcept -> int32_t {
	return 0;
}
}

#endif
