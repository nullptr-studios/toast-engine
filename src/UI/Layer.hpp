/**
 * @file Layper.hpp
 * @author Dante Harper
 * @date 19/01/26
 *
 * @brief [TODO: Brief description of the file's purpose]
 */

#pragma once

#include "Toast/Log.hpp"
#include "Toast/Renderer/ILayer.hpp"
#include "Toast/Renderer/IRenderable.hpp"

namespace ui {
class UiLayer final : public renderer::ILayer {
public:
	UiLayer() : ILayer("Game Ui Layer") { }

	void OnAttach() override { }

	void OnDetach() override { }

	void OnTick() override { }

	void OnRender() override {
		for (auto& ui_element : m.renerables) {
			ui_element->OnRender({});
		}
	}

	void Push(renderer::IRenderable* ptr) {
		m.renerables.push_back(ptr);
	}

	void Pop(renderer::IRenderable* ptr) {
		auto loc = std::ranges::find(m.renerables, ptr);
		if (loc == m.renerables.end()) {
			TOAST_WARN("Tried to remove IRenderable from Layer when IRenderable is not apart of the layer meow");
			return;
		}
		renderer::IRenderable* last = m.renerables.back();
		if (last == *loc) {
			m.renerables.pop_back();
			return;
		}
		*loc = last;
		m.renerables.pop_back();
	}

private:
	struct {
		std::vector<renderer::IRenderable*> renerables;
	} m;
};
}
