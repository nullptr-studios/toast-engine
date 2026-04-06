#pragma once

#include "Toast/Renderer/IPostProcess.hpp"
#include "Toast/Renderer/PostProcessing/Bloom.hpp"
#include "Toast/Renderer/PostProcessing/ChromaticAberration.hpp"
#include "Toast/Renderer/PostProcessing/ColorGrading.hpp"
#include "Toast/Renderer/PostProcessing/DepthOfField.hpp"
#include "Toast/Renderer/PostProcessing/Tonemaping.hpp"

#include <memory>
#include <string>
#include <vector>

namespace renderer::post {

inline std::unique_ptr<IPostProcess> CreateByType(const std::string& typeId) {
	if (typeId == "Tonemaping") {
		return std::make_unique<Tonemaping>();
	}
	if (typeId == "ColorGrading") {
		return std::make_unique<Colorgrading>();
	}
	if (typeId == "Bloom") {
		return std::make_unique<Bloom>();
	}
	if (typeId == "ChromaticAberration") {
		return std::make_unique<ChromaticAberration>();
	}
	if (typeId == "DepthOfField") {
		return std::make_unique<DepthOfField>();
	}
	return nullptr;
}

inline const std::vector<std::string>& AvailableTypes() {
	static const std::vector<std::string> kTypes = {
		"Tonemaping",
		"ColorGrading",
		"Bloom",
		"ChromaticAberration",
		"DepthOfField",
	};
	return kTypes;
}

}


