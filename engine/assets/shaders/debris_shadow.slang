// Opaque debris voxels into the shadow maps as moving casters
import shadow_views;

/// Mirrors renderer::ShadowPass::DebrisShadowPush
struct PushConstants {
    float extrapolation;
    float voxelSize;
    uint listCapacity;
    uint viewIndex;
};

[[vk::push_constant]]
PushConstants gPush;

// Mirrors physics::DebrisSimulation::GpuParticle and the set 1 of debris_draw.slang so DebrisPass sets bind here
struct DebrisParticle {
    float3 position;
    uint packed;
    float4 rotation;
    float3 velocity;
    uint albedo;
    float scale;
    uint glass;
    float emissive;
    uint pad1;
};

[[vk::binding(0, 1)]]
StructuredBuffer<DebrisParticle> gParticles;
[[vk::binding(1, 1)]]
StructuredBuffer<uint> gDrawList;

static const float3 kDebrisBoxCorners[8] = {
    float3(0.0, 0.0, 0.0), float3(1.0, 0.0, 0.0), float3(1.0, 1.0, 0.0), float3(0.0, 1.0, 0.0),
    float3(0.0, 0.0, 1.0), float3(1.0, 0.0, 1.0), float3(1.0, 1.0, 1.0), float3(0.0, 1.0, 1.0),
};

static const uint kDebrisBoxIndices[36] = {
    0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
    3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5,
};

float3 debrisRotate(float4 q, float3 v) {
    const float3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

[shader("vertex")]
float4 vertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID, uint viewId : SV_ViewID) : SV_Position {
    if (instanceId >= gPush.listCapacity) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    const uint entry = gDrawList[instanceId];
    const DebrisParticle particle = gParticles[entry >> 3];
    const uint bit = entry & 7u;
    if (((particle.packed >> bit) & 1u) == 0u || particle.scale <= 0.0) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    // Same box as debrisVertex in debris_draw.slang
    const float3 cell = float3(float(bit & 1u), float((bit >> 1) & 1u), float(bit >> 2));
    const float3 boxLocal = cell - 1.0 + kDebrisBoxCorners[kDebrisBoxIndices[vertexId]];
    const float3 center = particle.position + particle.velocity * gPush.extrapolation;
    const float3 world = center + debrisRotate(particle.rotation, boxLocal * (gPush.voxelSize * particle.scale));
    return mul(gShadow.viewProjection[gPush.viewIndex + viewId], float4(world, 1.0));
}
