// Opaque debris into the voxel G-buffer and glass debris blended forward like voxel glass
import voxel_gbuffer;
import lighting;
import glass_common;

struct PushConstants {
    float extrapolation;
    float frameDelta;
    float voxelSize;
    uint listCapacity;
};

[[vk::push_constant]]
PushConstants gPush;

// Mirrors physics::DebrisSimulation::GpuParticle
struct DebrisParticle {
    float3 position;
    uint packed;
    float4 rotation;
    float3 velocity;
    uint albedo;
    float scale;
    uint glass;
    uint pad0;
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

    [[vk::location(5)]]
    nointerpolation uint glass : TEXCOORD5;

    /// Piece space in unscaled voxels where this voxel spans cellMin to cellMin + 1
    [[vk::location(6)]]
    float3 boxLocal : TEXCOORD6;

    [[vk::location(7)]]
    nointerpolation float3 cameraLocal : TEXCOORD7;

    [[vk::location(8)]]
    nointerpolation float3 cellMin : TEXCOORD8;

    [[vk::location(9)]]
    nointerpolation float scale : TEXCOORD9;
};

float3 debrisRotate(float4 q, float3 v) {
    const float3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

float3 debrisSrgbToLinear(float3 value) {
    return lerp(pow((value + 0.055) / 1.055, 2.4), value / 12.92, step(value, float3(0.04045, 0.04045, 0.04045)));
}

VSOutput emptyVertex() {
    VSOutput output;
    output.position = float4(0.0, 0.0, 0.0, 1.0);
    output.worldPosition = float3(0.0, 0.0, 0.0);
    output.previousWorldPosition = float3(0.0, 0.0, 0.0);
    output.normal = float3(0.0, 0.0, 1.0);
    output.albedo = 0u;
    output.packed = 0u;
    output.glass = 0u;
    output.boxLocal = float3(0.0, 0.0, 0.0);
    output.cameraLocal = float3(0.0, 0.0, 0.0);
    output.cellMin = float3(0.0, 0.0, 0.0);
    output.scale = 0.0;
    return output;
}

VSOutput debrisVertex(uint entry, uint vertexId) {
    DebrisParticle particle = gParticles[entry >> 3];
    uint bit = entry & 7u;
    if (((particle.packed >> bit) & 1u) == 0u || particle.scale <= 0.0) {
        return emptyVertex();
    }

    // The block spans one voxel either side of its center
    float3 corner = kDebrisBoxCorners[kDebrisBoxIndices[vertexId]];
    float3 cell = float3(float(bit & 1u), float((bit >> 1) & 1u), float(bit >> 2));
    float3 boxLocal = cell - 1.0 + corner;
    float worldScale = gPush.voxelSize * particle.scale;

    float3 center = particle.position + particle.velocity * gPush.extrapolation;
    float3 world = center + debrisRotate(particle.rotation, boxLocal * worldScale);
    float4 inverse = float4(-particle.rotation.xyz, particle.rotation.w);

    VSOutput output;
    output.position = mul(gCamera.jitteredViewProjection, float4(world, 1.0));
    output.worldPosition = world;
    output.previousWorldPosition = world - particle.velocity * gPush.frameDelta;
    output.normal = debrisRotate(particle.rotation, kDebrisFaceNormals[vertexId / 6u]);
    output.albedo = particle.albedo;
    output.packed = particle.packed;
    output.glass = particle.glass;
    output.boxLocal = boxLocal;
    output.cameraLocal = debrisRotate(inverse, gCamera.position - center) / max(worldScale, 1e-6);
    output.cellMin = cell - 1.0;
    output.scale = particle.scale;
    return output;
}

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID) {
    if (instanceId >= gPush.listCapacity) {
        return emptyVertex();
    }
    return debrisVertex(gDrawList[instanceId], vertexId);
}

/// Glass entries fill the draw list from the top
[shader("vertex")]
VSOutput vertexGlass(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID) {
    if (instanceId >= gPush.listCapacity) {
        return emptyVertex();
    }
    return debrisVertex(gDrawList[gPush.listCapacity - 1u - instanceId], vertexId);
}

float3 debrisAlbedo(uint albedo) {
    return debrisSrgbToLinear(float3(albedo & 0xFFu, (albedo >> 8) & 0xFFu, (albedo >> 16) & 0xFFu) / 255.0);
}

float debrisRoughness(uint albedo) {
    return clamp(float((albedo >> 24) & 0xFFu) / 255.0, 0.045, 1.0);
}

struct FSOutput {
    float4 albedo : SV_Target0;
    float4 faceNormal : SV_Target1;
    float4 material : SV_Target2;
    float4 motion : SV_Target3;
};

[shader("fragment")]
FSOutput fragmentMain(VSOutput input) {
    float metallic = float((input.packed >> 8) & 0xFFu) / 255.0;
    float reflectivity = float((input.packed >> 16) & 0xFFu) / 255.0;

    float4 clip = mul(gCamera.viewProjection, float4(input.worldPosition, 1.0));
    float4 previousClip = mul(gCamera.previousViewProjection, float4(input.previousWorldPosition, 1.0));

    FSOutput output;
    output.albedo = float4(debrisAlbedo(input.albedo), kGbufferLit);
    output.faceNormal = encodeFaceNormal(normalize(input.normal));
    output.material = float4(metallic, reflectivity, debrisRoughness(input.albedo), encodeEmissive(0.0));
    output.motion = float4((clip.xy / clip.w - previousClip.xy / previousClip.w) * 0.5, 0.0, 1.0);
    return output;
}

struct GlassShade {
    float3 color;
    float3 transmittance;
};

/// Same optics as voxel_glass shadeInterface with the path through this one voxel
GlassShade shadeDebrisGlass(VSOutput input) {
    const float3 albedo = debrisAlbedo(input.albedo);
    const float roughness = debrisRoughness(input.albedo);
    const float alpha = float(input.glass & 0xFFu) / 255.0;
    const float ior = glassIor((input.glass >> 8) & 0xFFu);
    const float3 normal = normalize(input.normal);
    const float3 view = normalize(gCamera.position - input.worldPosition);

    Surface surface;
    surface.marchedShadows = false;
    surface.worldPosition = input.worldPosition;
    surface.normal = normal;
    surface.geometricNormal = normal;
    surface.viewDirection = view;
    surface.albedo = albedo;
    // Dielectric with the specular lobe always on so the IOR alone sets reflectance
    surface.metallic = 0.0;
    surface.reflectivity = 1.0;
    surface.roughness = roughness;
    surface.occlusion = 1.0;
    surface.viewSpaceZ = mul(gCamera.view, float4(input.worldPosition, 1.0)).z;
    surface.pixel = input.position.xy;

    GlassShade shade;
    const ShadingResult lit = shadeSurfaceLighting(surface);
    if (lit.isDebugView) {
        shade.color = lit.color;
        shade.transmittance = float3(0.0, 0.0, 0.0);
        return shade;
    }

    const float f0 = iorF0(ior);
    const float facing = pow(1.0 - saturate(dot(normal, view)), 5.0);
    const float fresnel = f0 + (1.0 - f0) * facing;
    const float scatter = glassScatter(alpha, roughness);

    const float3 diffuse = max(lit.color - lit.specular, float3(0.0, 0.0, 0.0));
    // Lighting assumes F0 0.04 so rescale by the Fresnel ratio
    const float3 specular = lit.specular * fresnel / (0.04 + 0.96 * facing);
    shade.color = diffuse * scatter + specular;

    // Front faces only so the exit face is the far slab of this voxel
    const float3 direction = normalize(input.boxLocal - input.cameraLocal);
    const float3 safe = select(abs(direction) > 1e-6, direction, float3(1e-6, 1e-6, 1e-6));
    const float3 far = max((input.cellMin - input.boxLocal) / safe, (input.cellMin + 1.0 - input.boxLocal) / safe);
    const float chord = max(min(far.x, min(far.y, far.z)), 0.0);

    shade.transmittance = (1.0 - fresnel) * (1.0 - scatter) * (1.0 - f0) *
                          glassPathFilter(albedo, alpha, chord * input.scale);
    return shade;
}

// Location 0 index 1 is the second blend source so the background is tinted not just faded
struct GlassOutput {
    [[vk::location(0)]]
    [[vk::index(0)]]
    float4 color : SV_Target0;

    [[vk::location(0)]]
    [[vk::index(1)]]
    float4 transmittance : SV_Target1;
};

[shader("fragment")]
GlassOutput fragmentGlass(VSOutput input) {
    const GlassShade shade = shadeDebrisGlass(input);
    GlassOutput output;
    output.color = float4(shade.color, 1.0);
    output.transmittance = float4(shade.transmittance, 1.0);
    return output;
}

/// Without dualSrcBlend the tint is lost and the background only fades
[shader("fragment")]
float4 fragmentGlassPremultiplied(VSOutput input) : SV_Target0 {
    const GlassShade shade = shadeDebrisGlass(input);
    return float4(shade.color, 1.0 - dot(shade.transmittance, float3(1.0, 1.0, 1.0)) / 3.0);
}
