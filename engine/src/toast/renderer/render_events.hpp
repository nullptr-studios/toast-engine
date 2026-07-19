/**
 * @file render_events.hpp
 * @author Xein
 * @date 17 Jul 2026
 *
 * @brief Editor-facing render events
 */

#pragma once
#include <string>
#include <toast/events/event.hpp>
#include <vector>

namespace event {

struct RequestRenderPasses : Event<RequestRenderPasses> { };

struct RenderPassList : Event<RenderPassList> {
	struct PassInfo {
		std::string name;
		bool enabled = true;
	};

	std::vector<PassInfo> passes;

	RenderPassList() = default;

	explicit RenderPassList(std::vector<PassInfo> passes) : passes(std::move(passes)) { }
};

struct SetRenderPassEnabled : Event<SetRenderPassEnabled> {
	std::string name;
	bool enabled = true;

	SetRenderPassEnabled() = default;

	SetRenderPassEnabled(std::string_view name, bool enabled) : name(name), enabled(enabled) { }
};

struct RequestShaderReflection : Event<RequestShaderReflection> {
	std::vector<std::string> shader_uids;

	RequestShaderReflection() = default;

	explicit RequestShaderReflection(std::vector<std::string> uids) : shader_uids(std::move(uids)) { }
};

struct ShaderReflectionReady : Event<ShaderReflectionReady> {
	std::vector<std::string> shader_uids;
	bool success = true;
	std::string error;

	ShaderReflectionReady() = default;

	ShaderReflectionReady(std::vector<std::string> uids, bool success, std::string_view error)
	    : shader_uids(std::move(uids)),
	      success(success),
	      error(error) { }
};

}

namespace renderer {

void registerRenderEvents();

}
