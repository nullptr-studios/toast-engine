/// @file IPostProcess.hpp
/// @author dario
/// @date 23/03/2026.

#pragma once

#include "Framebuffer.hpp"
#include "Toast/ISerializable.hpp"

#include <string_view>

struct IPostProcess {
	virtual ~IPostProcess() = default;

	[[nodiscard]]
	virtual std::string_view GetTypeId() const = 0;

	virtual void Execute(Framebuffer* inputFBO, Framebuffer* outputFBO) = 0;

	[[nodiscard]]
	virtual json_t SaveParams() const = 0;
	virtual void LoadParams(const json_t& j) = 0;

	[[nodiscard]]
	json_t SaveState() const {
		json_t j {};
		j["type"] = GetTypeId();
		j["enabled"] = m_enabled;
		j["blend"] = m_blend;
		j["params"] = SaveParams();
		return j;
	}

	void LoadState(const json_t& j) {
		m_enabled = j.value("enabled", true);
		m_blend = j.value("blend", 1.0f);
		if (j.contains("params")) {
			LoadParams(j.at("params"));
		} else {
			LoadParams(j);
		}
	}

	[[nodiscard]]
	bool IsEnabled() const {
		return m_enabled;
	}

	void SetEnabled(bool enabled) {
		m_enabled = enabled;
	}

	[[nodiscard]]
	float GetBlend() const {
		return m_blend;
	}

	void SetBlend(float blend) {
		m_blend = blend;
	}

	// virtual json_t Save() = 0;
	// virtual void Load(json_t&) = 0;

#ifdef TOAST_EDITOR
	virtual void Inspector() = 0;
#endif

private:
	bool m_enabled = true;
	float m_blend = 1.0f;
};
