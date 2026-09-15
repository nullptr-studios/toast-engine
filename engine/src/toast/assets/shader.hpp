/**
 * @file shader.hpp
 * @author Xein
 * @date 17 Jul 2026
 *
 * @brief Slang shader source asset, compiled to SPIR-V by the renderer's ShaderCache
 */

#pragma once
#include "core_types.hpp"

namespace assets {

class TOAST_API Shader : public Asset {
public:
	explicit Shader(std::vector<uint8_t> data) : m_source(reinterpret_cast<const char*>(data.data()), data.size()) { }

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "shader";
	}

	[[nodiscard]]
	auto source() const noexcept -> const std::string& {
		return m_source;
	}

	/// Replaces the source in place on hot reload
	void setSource(std::vector<uint8_t> data) noexcept { m_source.assign(reinterpret_cast<const char*>(data.data()), data.size()); }

private:
	std::string m_source;
};

}
