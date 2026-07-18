/**
 * @file panel_3d.hpp
 * @author Xein
 * @date 18 Jul 2026
 *
 * @brief World-space UI panel
 */

#pragma once
#include <memory>
#include <toast/ui/assets.hpp>
#include <toast/world/node_3d.hpp>

namespace Rml {
class Context;
class ElementDocument;
}

namespace ui {
class UIBinds;
}

namespace toast {

class TOAST_API [[ToastNode, Icon("Window"), Color("Blue")]] Panel3D : public Node3D {
public:
	Panel3D();
	~Panel3D() override;

	[[nodiscard]]
	auto rmlContext() -> Rml::Context* {
		return m_context;
	}

	[[nodiscard]]
	auto rmlDocument() -> Rml::ElementDocument* {
		return m_document;
	}

	[[nodiscard]]
	auto colorScheme() const -> const assets::AssetHandle<assets::ColorScheme>& {
		return m_color_scheme;
	}

	[[nodiscard]]
	auto pixelSize() const -> glm::ivec2;

	[[nodiscard]]
	auto worldTransformForRender() -> const glm::mat4& {
		return getWorldTransform();
	}

	void reloadDocument();

private:
	void init();
	void destroy();
	void onEnable();
	void onDisable();
	void tick();

	void loadDocument();
	void unloadDocument();

	[[Reflect]]
	assets::AssetHandle<assets::UIElement> m_element;

	[[Reflect]]
	std::vector<assets::AssetHandle<assets::UIStyle>> m_styles;

	[[Reflect]]
	std::vector<assets::AssetHandle<assets::Font>> m_fonts;

	[[Reflect]]
	std::vector<assets::AssetHandle<assets::FontFamily>> m_font_families;

	[[Reflect]]
	assets::AssetHandle<assets::ColorScheme> m_color_scheme;

	/// Texture resolution per meter of world scale
	[[Reflect]]
	float m_pixels_per_meter = 512.0f;
	// TODO: We should probably calculate this depending on the distance to the object

	Rml::Context* m_context = nullptr;
	Rml::ElementDocument* m_document = nullptr;
	std::unique_ptr<ui::UIBinds> m_binds;
};

}
