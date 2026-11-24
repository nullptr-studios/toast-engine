/// @file game_pass.hpp
/// @author dario
/// @date 16/11/2026

#pragma once

#include "toast/export.hpp"

#include <cstdint>

/**
 * @brief Custom render passes from game code
 *
 * The engine owns the pipeline, descriptors and buffers. The game only gives a shader, a few plain settings and
 * an array of instance structs each frame
 *
 * Each instance draws vertices_per_instance vertices with no vertex buffer, the vertex shader builds the geometry
 * from SV_InstanceID and SV_VertexID
 *
 * Usage:
 * @code
 * struct Spark { glm::vec3 position; float size; };
 *
 * // begin()
 * m_pass = renderer::createGamePass({
 *   .name = "Sparks",
 *   .shader_uri = "assets://Shaders/spark.slang",
 *   .instance_stride = sizeof(Spark),
 *   .max_instances = 512,
 * });
 *
 * // every tick, the last call stays drawn until the next one
 * renderer::setGamePassInstances(m_pass, m_sparks.data(), static_cast<uint32_t>(m_sparks.size()));
 *
 * // end(), also before the game module unloads
 * renderer::destroyGamePass(std::exchange(m_pass, nullptr));
 * @endcode
 *
 * To draw into a texture, create a view and point passes at it. RmlUi shows it with <img src="view://Minimap"/>
 * @code
 * m_view = renderer::createGameView({.name = "Minimap", .width = 256, .height = 256, .depth = true});
 * m_pass = renderer::createGamePass({.name = "Blips", .shader_uri = "...", .view = m_view, .instance_stride = 16});
 * renderer::destroyGamePass(std::exchange(m_pass, nullptr));
 * renderer::destroyGameView(std::exchange(m_view, nullptr));
 * @endcode
 */

namespace renderer {

enum class GamePassStage : uint8_t {
	/// HDR scene before TAA, bloom and tonemap
	world,
	/// After the tonemap at output resolution, use viewProjection
	overlay,
};

enum class GamePassBlend : uint8_t {
	opaque,
	/// srcAlpha, 1 - srcAlpha
	alpha,
	/// one, one
	additive,
};

class GameView;

/// RGBA8 unorm with premultiplied alpha
struct GameViewDesc {
	/// Unique and without /
	const char* name = nullptr;
	uint32_t width = 256;
	uint32_t height = 256;
	float clear_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	/// Own depth attachment cleared every frame
	bool depth = false;
};

auto TOAST_API createGameView(const GameViewDesc& desc) -> GameView*;

/// Passes still pointing at the view stop drawing
void TOAST_API destroyGameView(GameView* view);

struct GamePassDesc {
	const char* name = "Game pass";
	const char* shader_uri = nullptr;
	/// Draws into this view instead of the scene, ignoring stage and reactive
	GameView* view = nullptr;
	GamePassStage stage = GamePassStage::world;
	GamePassBlend blend = GamePassBlend::additive;
	bool depth_test = true;
	bool depth_write = false;
	/// World stage and blended only. Lets thin or short lived effects survive TAA: the shader also writes
	/// SV_Target1-3 (normal, indirect, motion) blended onto the scene, zeros leave them untouched and motion z 1
	/// makes TAA show the pixel this frame
	bool reactive = false;
	/// sizeof the instance struct, required
	uint32_t instance_stride = 0;
	/// Extra instances passed to setGamePassInstances are dropped
	uint32_t max_instances = 256;
	/// 6 for a quad as two triangles
	uint32_t vertices_per_instance = 6;
};

class GamePass;

auto TOAST_API createGamePass(const GamePassDesc& desc) -> GamePass*;

/// Any thread Copies the data. A count of 0 draws nothing
void TOAST_API setGamePassInstances(GamePass* pass, const void* data, uint32_t count);

/// The GPU objects are freed once no frame in flight uses them
void TOAST_API destroyGamePass(GamePass* pass);

}
