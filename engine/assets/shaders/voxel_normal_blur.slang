import fullscreen;
import voxel_gbuffer;

// Mirrors VoxelPass::NormalBlurParams
struct NormalBlurParams
{
    // Must be jittered since depth was rasterized with it
    float4x4 inverseViewProjection;
    // xyz camera position w rounding radius in world units
    float4 cameraRadius;
    // xy viewport size z pixels per world unit at 1 m w spiral rotation in turns
    float4 viewport;
};

[[vk::push_constant]]
ConstantBuffer<NormalBlurParams> gParams;

[[vk::binding(0, 0)]]
Sampler2D gAlbedo;

[[vk::binding(1, 0)]]
Sampler2D gFaceNormal;

[[vk::binding(2, 0)]]
Sampler2D gDepth;

typedef FullscreenVSOutput VSOutput;

static const uint kTaps = 16;
static const float kGoldenAngle = 2.39996323;

// Sixteen taps spread wider than this stop reading as one smooth bevel
static const float kMaxRadiusPixels = 32.0;

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    return fullscreenTriangle(vertexId);
}

float3 positionAt(int2 pixel)
{
    const float2 uv = (float2(pixel) + 0.5) / gParams.viewport.xy;
    const float depth = gDepth.Load(int3(pixel, 0)).r;
    const float4 world = mul(gParams.inverseViewProjection, float4(uv * 2.0 - 1.0, depth, 1.0));
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

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    const int2 pixel = int2(input.position.xy);
    if (!isLitVoxelAt(pixel))
    {
        discard;
    }

    const float4 face = gFaceNormal.Load(int3(pixel, 0));
    const float radius = gParams.cameraRadius.w;
    if (radius <= 0.0)
    {
        return face;
    }

    const float3 center = positionAt(pixel);
    const float distanceToCamera = max(distance(center, gParams.cameraRadius.xyz), 1e-4);

    // A fixed world radius shrinks under a pixel with distance so rounding stops there
    const float radiusPixels = min(radius * gParams.viewport.z / distanceToCamera, kMaxRadiusPixels);
    if (radiusPixels < 1.0)
    {
        return face;
    }

    const float rotation = (noiseAt(input.position.xy) + gParams.viewport.w) * 6.28318531;
    const int2 size = int2(gParams.viewport.xy);

    float3 sum = face.xyz;
    for (uint i = 0u; i < kTaps; ++i)
    {
        const float angle = float(i) * kGoldenAngle + rotation;
        const float2 offset = sqrt((float(i) + 0.5) / float(kTaps)) * radiusPixels * float2(cos(angle), sin(angle));
        const int2 tap = int2(floor(input.position.xy + offset));
        if (any(tap < int2(0, 0)) || any(tap >= size) || !isLitVoxelAt(tap))
        {
            continue;
        }

        // Weighted by world distance so an occluded or grazing neighbour drops out
        const float3 toTap = positionAt(tap) - center;
        const float falloff = saturate(1.0 - dot(toTap, toTap) / (radius * radius));
        sum += gFaceNormal.Load(int3(tap, 0)).xyz * (falloff * falloff);
    }

    return float4(normalize(sum), face.w);
}
