#include "render_events.hpp"

#include "shader_cache.hpp"
#include "vulkan_renderer.hpp"

#include <format>
#include <generated/render_events.pb.h>
#include <toast/events/listener.hpp>
#include <toast/events/proto_event.hpp>
#include <toast/log.hpp>
#include <toast/uid.hpp>

namespace event {

template<>
struct ProtoTraits<RequestRenderPasses> {
	using Proto = proto::events::RequestRenderPasses;
	using Event = RequestRenderPasses;

	static auto toProto(const Event&) -> Proto { return {}; }

	static auto fromProto(const Proto&) -> Event { return {}; }
};

TOAST_PROTO_EVENT(RequestRenderPasses);

template<>
struct ProtoTraits<RenderPassList> {
	using Proto = proto::events::RenderPassList;
	using Event = RenderPassList;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		for (const auto& pass : e.passes) {
			auto* info = p.add_passes();
			info->set_name(pass.name);
			info->set_enabled(pass.enabled);
		}
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.passes.reserve(p.passes_size());
		for (const auto& info : p.passes()) {
			e.passes.push_back(RenderPassList::PassInfo {.name = info.name(), .enabled = info.enabled()});
		}
		return e;
	}
};

TOAST_PROTO_EVENT(RenderPassList);

template<>
struct ProtoTraits<SetRenderPassEnabled> {
	using Proto = proto::events::SetRenderPassEnabled;
	using Event = SetRenderPassEnabled;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		p.set_name(e.name);
		p.set_enabled(e.enabled);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event { return {p.name(), p.enabled()}; }
};

TOAST_PROTO_EVENT(SetRenderPassEnabled);

template<>
struct ProtoTraits<RequestShaderReflection> {
	using Proto = proto::events::RequestShaderReflection;
	using Event = RequestShaderReflection;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		for (const auto& uid : e.shader_uids) {
			p.add_shader_uids(uid);
		}
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.shader_uids.assign(p.shader_uids().begin(), p.shader_uids().end());
		return e;
	}
};

TOAST_PROTO_EVENT(RequestShaderReflection);

template<>
struct ProtoTraits<ShaderReflectionReady> {
	using Proto = proto::events::ShaderReflectionReady;
	using Event = ShaderReflectionReady;

	static auto toProto(const Event& e) -> Proto {
		Proto p;
		for (const auto& uid : e.shader_uids) {
			p.add_shader_uids(uid);
		}
		p.set_success(e.success);
		p.set_error(e.error);
		return p;
	}

	static auto fromProto(const Proto& p) -> Event {
		Event e;
		e.shader_uids.assign(p.shader_uids().begin(), p.shader_uids().end());
		e.success = p.success();
		e.error = p.error();
		return e;
	}
};

TOAST_PROTO_EVENT(ShaderReflectionReady);

}

namespace renderer {

namespace {

void sendPassList() {
	if (VulkanRenderer::instance == nullptr) {
		return;
	}

	std::vector<event::RenderPassList::PassInfo> passes;
	for (const auto& pass : VulkanRenderer::instance->listPasses()) {
		passes.push_back(event::RenderPassList::PassInfo {.name = pass.name, .enabled = pass.enabled});
	}
	event::send<event::RenderPassList>(std::move(passes));
}

}

void registerRenderEvents() {
	static bool done = false;
	if (done) {
		return;
	}
	done = true;

	static event::Listener listener;

	listener.subscribe<event::RequestRenderPasses>([] {
		sendPassList();
		return false;
	});

	listener.subscribe<event::SetRenderPassEnabled>([](const event::SetRenderPassEnabled& e) {
		if (VulkanRenderer::instance != nullptr) {
			VulkanRenderer::instance->setPassEnabled(e.name, e.enabled);
			sendPassList();
		}
		return false;
	});

	listener.subscribe<event::RequestShaderReflection>([](const event::RequestShaderReflection& e) {
		bool success = true;
		std::string error;

		// ensureCompiled writes both the SPIR-V and reflection json before returning,
		// so the editor can safely read cache://shaders once the reply arrives
		for (const auto& uid_str : e.shader_uids) {
			const toast::UID uid(toast::UID::fromString(uid_str));
			if (uid.data() == 0 || !ShaderCache::get().ensureCompiled(uid)) {
				success = false;
				error = std::format("Shader {} failed to compile", uid_str);
				TOAST_WARN("ShaderCache", "Editor reflection request failed for {}", uid_str);
			}
		}

		event::send<event::ShaderReflectionReady>(e.shader_uids, success, error);
		return false;
	});
}

}
