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
 * an array of instance structs each frame, so no Vulkan or STL type crosses the DLL boundary and a game module
 * built in Debug works against a Release engine.
 *
 * Each instance draws vertices_per_instance vertices with no vertex buffer, the vertex shader builds the geometry
 * from SV_InstanceID and SV_VertexID.
 *
 * Shader contract:
 * @code
 * struct CameraUBO                     // Prefix of the engine FrameUBO, keep the field order
 * {
 *     float4x4 view;
 *     float4x4 projection;
 *     float4x4 viewProjection;
 *     float3 cameraPosition;
 *     float time;
 *     float4x4 jitteredViewProjection; // World stage uses this one so it matches the scene depth under TAA
 * };
 * [[vk::binding(0, 0)]] ConstantBuffer<CameraUBO> gCamera;
 *
 * struct Instance { ... };             // Same layout as the C++ struct, mind float3 + float packing
 * [[vk::binding(1, 0)]] StructuredBuffer<Instance> gInstances;
 *
 * [shader("vertex")]   VSOutput vertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID);
 * [shader("fragment")] float4 fragmentMain(VSOutput input) : SV_Target0;
 * // With reactive the fragment returns SV_Target0 color, SV_Target1 normal, SV_Target2 indirect, SV_Target3 motion
 * @endcode
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
 * Instances belong to the pass, not to a node, so whoever stops updating them leaves them frozen on screen.
 * See the shared TracerSystem in JhonMafia weapon_manager.cpp for one pass fed by many nodes.
 */

namespace renderer {

enum class GamePassStage : uint8_t {
	/// HDR scene before TAA, bloom and tonemap, depth tested against the scene
	world,
	/// After the tonemap at output resolution, use viewProjection and LDR colors
	overlay,
};

enum class GamePassBlend : uint8_t {
	opaque,
	/// srcAlpha, 1 - srcAlpha
	alpha,
	/// one, one
	additive,
};

struct GamePassDesc {
	const char* name = "Game pass";
	/// Game assets use assets://, engine ones core://
	const char* shader_uri = nullptr;
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

/// Main thread. Null when the renderer is missing, the shader is unavailable or instance_stride is 0
auto TOAST_API createGamePass(const GamePassDesc& desc) -> GamePass*;

/// Any thread. Copies the data, a count of 0 draws nothing
void TOAST_API setGamePassInstances(GamePass* pass, const void* data, uint32_t count);

/// Any thread and null safe. The GPU objects are freed once no frame in flight uses them
void TOAST_API destroyGamePass(GamePass* pass);

}
