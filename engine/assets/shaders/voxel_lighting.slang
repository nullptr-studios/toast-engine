import fullscreen;
import voxel_gbuffer;
import lighting;

// Mirrors VoxelPass::LightingPushConstants
struct LightingPushConstants
{
    // Must be jittered since depth was rasterized with it
    float4x4 inverseViewProjection;
    // xyz camera position w rounding radius in world units
    float4 cameraRadius;
    // xy viewport size z pixels per world unit at 1 m w spiral rotation in turns
    float4 viewport;
};

[[vk::push_constant]]
ConstantBuffer<LightingPushConstants> gPush;

[[vk::binding(0, 1)]]
Sampler2D gAlbedo;

// Unrounded so shadow bias still sees the flat face
[[vk::binding(1, 1)]]
Sampler2D gFaceNormal;

[[vk::binding(2, 1)]]
Sampler2D gMaterial;

[[vk::binding(3, 1)]]
Sampler2D gDepth;

typedef FullscreenVSOutput VSOutput;

struct FSOutput
{
    float4 color : SV_Target0;
    float4 indirectDiffuse : SV_Target1;
    /// xyz rounded normal w SSR roughness and zero where SSAO and SSR pass through
    float4 normalRoughness : SV_Target2;
};

/// One tap per pixel of radius within this range
static const uint kMinRoundingTaps = 4;
static const uint kMaxRoundingTaps = 16;
static const float kGoldenAngle = 2.39996323;

// kMaxRoundingTaps spread wider than this stop reading as one smooth bevel
static const float kMaxRoundingPixels = 32.0;

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    return fullscreenTriangle(vertexId);
}

float3 positionAt(int2 pixel)
{
    const float2 uv = (float2(pixel) + 0.5) / gPush.viewport.xy;
    const float depth = gDepth.Load(int3(pixel, 0)).r;
    const float4 world = mul(gPush.inverseViewProjection, float4(uv * 2.0 - 1.0, depth, 1.0));
    return world.xyz / world.w;
}

bool isLitVoxelAt(int2 pixel)
{
    return isLitVoxel(gAlbedo.Load(int3(pixel, 0)).a);
}

// Interleaved gradient noise
float noiseAt(float2 pixel)
{
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

/// Averages lit neighbour face normals by world distance so voxel edges read as bevels
float3 roundedNormal(float2 pixel, float3 center, float3 face)
{
    const float radius = gPush.cameraRadius.w;
    if (radius <= 0.0)
    {
        return face;
    }

    // A fixed world radius shrinks under a pixel with distance so rounding stops there
    const float distanceToCamera = max(distance(center, gPush.cameraRadius.xyz), 1e-4);
    const float radiusPixels = min(radius * gPush.viewport.z / distanceToCamera, kMaxRoundingPixels);
    if (radiusPixels < 1.0)
    {
        return face;
    }

    const float rotation = (noiseAt(pixel) + gPush.viewport.w) * 6.28318531;
    const int2 size = int2(gPush.viewport.xy);

    const uint taps = clamp(uint(radiusPixels), kMinRoundingTaps, kMaxRoundingTaps);
    float3 sum = face;
    [loop]
    for (uint i = 0u; i < taps; ++i)
    {
        const float angle = float(i) * kGoldenAngle + rotation;
        const float2 offset = sqrt((float(i) + 0.5) / float(taps)) * radiusPixels * float2(cos(angle), sin(angle));
        const int2 tap = int2(floor(pixel + offset));
        if (any(tap < int2(0, 0)) || any(tap >= size) || !isLitVoxelAt(tap))
        {
            continue;
        }

        // An occluded or grazing neighbour drops out
        const float3 toTap = positionAt(tap) - center;
        const float falloff = saturate(1.0 - dot(toTap, toTap) / (radius * radius));
        sum += decodeFaceNormal(gFaceNormal.Load(int3(tap, 0))) * (falloff * falloff);
    }
    return normalize(sum);
}

[shader("fragment")]
FSOutput fragmentMain(VSOutput input)
{
    const int2 pixel = int2(input.position.xy);
    const int3 texel = int3(pixel, 0);
    const float4 albedo = gAlbedo.Load(texel);
    if (!isVoxelCoverage(albedo.a))
    {
        discard;
    }

    FSOutput output;
    output.indirectDiffuse = float4(0.0, 0.0, 0.0, 1.0);
    output.normalRoughness = float4(0.0, 0.0, 0.0, 0.0);
    if (!isLitVoxel(albedo.a))
    {
        output.color = float4(albedo.rgb, 1.0);
        return output;
    }

    const float3 position = positionAt(pixel);
    const float3 faceNormal = decodeFaceNormal(gFaceNormal.Load(texel));
    const float3 normal = roundedNormal(input.position.xy, position, faceNormal);
    const float4 material = gMaterial.Load(texel);
    const float roughness = clamp(material.b, 0.045, 1.0);

    // SSR only reads roughness so zero reflectivity must look fully rough
    output.normalRoughness = float4(normal, lerp(1.0, roughness, material.g));

    // The near plane clip can put the hit on the camera
    const float3 toCamera = gCamera.position - position;
    const float3 view = dot(toCamera, toCamera) > 1e-10 ? normalize(toCamera) : normal;

    Surface surface;
    surface.worldPosition = position;
    surface.normal = normal;
    surface.geometricNormal = faceNormal;
    surface.viewDirection = view;
    surface.albedo = albedo.rgb;
    surface.metallic = material.r;
    surface.reflectivity = material.g;
    surface.roughness = roughness;
    surface.occlusion = 1.0;
    surface.viewSpaceZ = mul(gCamera.view, float4(position, 1.0)).z;
    surface.pixel = input.position.xy;

    const ShadingResult lit = shadeSurfaceLighting(surface);
    output.indirectDiffuse = float4(lit.indirectDiffuse, 1.0);
    output.color = float4(lit.isDebugView ? lit.color : lit.color + albedo.rgb * decodeEmissive(material.a), 1.0);
    return output;
}
