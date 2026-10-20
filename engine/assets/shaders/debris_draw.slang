// Draws debris into the voxel G-buffer so they are like every other voxel
// I think this works maybe dario should review it
import voxel_gbuffer;
import lighting;

struct PushConstants {
    float extrapolation;
    float frameDelta;
    float voxelSize;
    uint listCapacity;
};

[[vk::push_constant]]
PushConstants gPush;

struct DebrisParticle {
    float3 position;
    uint packed;
    float4 rotation;
    float3 velocity;
    uint albedo;
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
    0, 2, 1, 0, 3, 2,    // -Z
    4, 5, 6, 4, 6, 7,    // +Z
    0, 1, 5, 0, 5, 4,    // -Y
    3, 7, 6, 3, 6, 2,    // +Y
    0, 4, 7, 0, 7, 3,    // -X
    1, 2, 6, 1, 6, 5,    // +X
};

static const float3 kDebrisFaceNormals[6] = {
    float3(0.0, 0.0, -1.0), float3(0.0, 0.0, 1.0), float3(0.0, -1.0, 0.0),
    float3(0.0, 1.0, 0.0), float3(-1.0, 0.0, 0.0), float3(1.0, 0.0, 0.0),
};

struct VSOutput {
    float4 position : SV_Position;

    [[vk::location(0)]]
    float3 worldPosition : TEXCOORD0;

    [[vk::location(1)]]
    float3 previousWorldPosition : TEXCOORD1;

    [[vk::location(2)]]
    nointerpolation float3 normal : TEXCOORD2;

    [[vk::location(3)]]
    nointerpolation uint albedo : TEXCOORD3;

    [[vk::location(4)]]
    nointerpolation uint packed : TEXCOORD4;
};

float3 debrisRotate(float4 q, float3 v) {
    const float3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

float3 debrisSrgbToLinear(float3 value) {
    return lerp(pow((value + 0.055) / 1.055, 2.4), value / 12.92, step(value, float3(0.04045, 0.04045, 0.04045)));
}

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID) {
    VSOutput output;
    output.position = float4(0.0, 0.0, 0.0, 1.0);
    output.worldPosition = float3(0.0, 0.0, 0.0);
    output.previousWorldPosition = float3(0.0, 0.0, 0.0);
    output.normal = float3(0.0, 0.0, 1.0);
    output.albedo = 0u;
    output.packed = 0u;

    if (instanceId >= gPush.listCapacity) {
        return output;
    }

    uint entry = gDrawList[instanceId];
    DebrisParticle particle = gParticles[entry >> 3];
    uint bit = entry & 7u;
    if (((particle.packed >> bit) & 1u) == 0u) {
        return output;
    }

    // The block spans one voxel either side of its center
    float3 corner = kDebrisBoxCorners[kDebrisBoxIndices[vertexId]];
    float3 cell = float3(float(bit & 1u), float((bit >> 1) & 1u), float(bit >> 2));
    float3 local = (cell - 1.0 + corner) * gPush.voxelSize;

    float3 center = particle.position + particle.velocity * gPush.extrapolation;
    float3 world = center + debrisRotate(particle.rotation, local);

    output.position = mul(gCamera.jitteredViewProjection, float4(world, 1.0));
    output.worldPosition = world;
    output.previousWorldPosition = world - particle.velocity * gPush.frameDelta;
    output.normal = debrisRotate(particle.rotation, kDebrisFaceNormals[vertexId / 6u]);
    output.albedo = particle.albedo;
    output.packed = particle.packed;
    return output;
}

struct FSOutput {
    float4 albedo : SV_Target0;
    float4 faceNormal : SV_Target1;
    float4 material : SV_Target2;
    float4 motion : SV_Target3;
};

[shader("fragment")]
FSOutput fragmentMain(VSOutput input) {
    float3 albedo = debrisSrgbToLinear(float3(input.albedo & 0xFFu, (input.albedo >> 8) & 0xFFu, (input.albedo >> 16) & 0xFFu) / 255.0);
    float roughness = clamp(float((input.albedo >> 24) & 0xFFu) / 255.0, 0.045, 1.0);
    float metallic = float((input.packed >> 8) & 0xFFu) / 255.0;
    float reflectivity = float((input.packed >> 16) & 0xFFu) / 255.0;

    float4 clip = mul(gCamera.viewProjection, float4(input.worldPosition, 1.0));
    float4 previousClip = mul(gCamera.previousViewProjection, float4(input.previousWorldPosition, 1.0));

    FSOutput output;
    output.albedo = float4(albedo, kGbufferLit);
    output.faceNormal = encodeFaceNormal(normalize(input.normal));
    output.material = float4(metallic, reflectivity, roughness, encodeEmissive(0.0));
    output.motion = float4((clip.xy / clip.w - previousClip.xy / previousClip.w) * 0.5, 0.0, 1.0);
    return output;
}
