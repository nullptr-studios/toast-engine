// Temporal per term then spatial diffuse and composite of the voxel_lighting.slang noisy terms
// Specular is temporal only since a spatial filter erases highlights
import fullscreen;
import depth_convention;
import voxel_gbuffer;

/// Mirrors renderer::VoxelPass::DenoisePush
struct DenoisePush
{
    /// Jittered since depth was rasterized with it
    float4x4 inverseViewProjection;
    float4x4 previousViewProjection;
    /// x term 0 diffuse 1 specular y 1 with a valid history z spiral rotation in turns w spiral radius in pixels
    float4 params;
    float4 viewport;
};

[[vk::push_constant]]
ConstantBuffer<DenoisePush> gPush;

[[vk::binding(0, 0)]]
Sampler2D gNoisyDiffuse;

[[vk::binding(1, 0)]]
Sampler2D gNoisySpecular;

[[vk::binding(2, 0)]]
Sampler2D gMotion;

[[vk::binding(3, 0)]]
Sampler2D gDepth;

[[vk::binding(4, 0)]]
Sampler2D gAlbedo;

[[vk::binding(5, 0)]]
Sampler2D gFaceNormal;

/// rgb term a linear depth
[[vk::binding(6, 0)]]
Sampler2D gDiffuseHistory;

/// a frames accumulated for both terms
[[vk::binding(7, 0)]]
Sampler2D gSpecularHistory;

[[vk::binding(8, 0)]]
Sampler2D gDiffuseCurrent;

[[vk::binding(9, 0)]]
Sampler2D gSpecularCurrent;

/// Blend floors so a settled pixel still follows changes
static const float kDiffuseBlend = 0.1;
static const float kSpecularBlend = 0.2;

static const float kMaxHistoryLength = 32.0;

/// Frames over which the spatial radius shrinks from widest to its setting
static const float kSettleFrames = 8.0;
static const float kFreshRadiusScale = 2.5;

/// Clip box half width in standard deviations
static const float kDiffuseClip = 2.0;
static const float kSpecularClip = 1.25;

/// Depth error share past which a history is another surface
static const float kDisocclusionDepth = 0.05;

static const uint kSpatialTaps = 12u;

static const float2 kSpiral[kSpatialTaps] = {
    float2(0.2041241, 0.0000000),
    float2(-0.2606993, 0.2388219),
    float2(0.0399042, -0.4546878),
    float2(0.3285945, 0.4285934),
    float2(-0.6030114, -0.1066642),
    float2(0.5712250, -0.3633666),
    float2(-0.1910636, 0.7107470),
    float2(-0.3643790, -0.7015896),
    float2(0.7905567, 0.2887100),
    float2(-0.8224425, 0.3394923),
    float2(0.3964716, -0.8472368),
    float2(0.2929824, 0.9340742)
};

typedef FullscreenVSOutput VSOutput;

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    return fullscreenTriangle(vertexId);
}

float3 worldPositionAt(float2 uv, float depth)
{
    const float4 world = mul(gPush.inverseViewProjection, float4(uv * 2.0 - 1.0, depth, 1.0));
    return world.xyz / world.w;
}

/// Clip w from the 1 / w row of the inverse view projection
float linearDepthAt(float2 uv, float depth)
{
    return 1.0 / max(dot(gPush.inverseViewProjection[3], float4(uv * 2.0 - 1.0, depth, 1.0)), 1e-8);
}

/// Only lit voxels write a face normal
bool litVoxelAt(int2 pixel)
{
    return gFaceNormal.Load(int3(pixel, 0)).a > 0.5;
}

float3 noisyAt(int2 pixel, bool specular)
{
    return specular ? gNoisySpecular.Load(int3(pixel, 0)).rgb : gNoisyDiffuse.Load(int3(pixel, 0)).rgb;
}

/// Bilinear at the expected depth so jitter across an edge keeps history and else the closest match one pixel around
bool reprojectHistory(float2 previousUv, float expectedDepth, out float4 diffuseHistory, out float4 specularHistory)
{
    diffuseHistory = float4(0.0, 0.0, 0.0, 0.0);
    specularHistory = float4(0.0, 0.0, 0.0, 0.0);
    const int2 size = int2(gPush.viewport.xy);
    const float tolerance = kDisocclusionDepth * expectedDepth;

    const float2 texel = previousUv * gPush.viewport.xy - 0.5;
    const int2 base = int2(floor(texel));
    const float2 f = texel - float2(base);
    float4 diffuseSum = float4(0.0, 0.0, 0.0, 0.0);
    float4 specularSum = float4(0.0, 0.0, 0.0, 0.0);
    float weightSum = 0.0;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        const int2 offset = int2(i & 1, i >> 1);
        const int2 tap = base + offset;
        if (any(tap < int2(0, 0)) || any(tap >= size))
        {
            continue;
        }
        const float4 diffuse = gDiffuseHistory.Load(int3(tap, 0));
        if (abs(diffuse.a - expectedDepth) > tolerance)
        {
            continue;
        }
        const float weight = (offset.x == 1 ? f.x : 1.0 - f.x) * (offset.y == 1 ? f.y : 1.0 - f.y);
        diffuseSum += diffuse * weight;
        specularSum += gSpecularHistory.Load(int3(tap, 0)) * weight;
        weightSum += weight;
    }
    if (weightSum > 1e-3)
    {
        diffuseHistory = diffuseSum / weightSum;
        specularHistory = specularSum / weightSum;
        return true;
    }

    const int2 nearest = int2(floor(previousUv * gPush.viewport.xy));
    float bestError = tolerance;
    bool found = false;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const int2 tap = nearest + int2(x, y);
            if (any(tap < int2(0, 0)) || any(tap >= size))
            {
                continue;
            }
            const float4 diffuse = gDiffuseHistory.Load(int3(tap, 0));
            const float error = abs(diffuse.a - expectedDepth);
            if (error <= bestError)
            {
                bestError = error;
                diffuseHistory = diffuse;
                specularHistory = gSpecularHistory.Load(int3(tap, 0));
                found = true;
            }
        }
    }
    return found;
}

[shader("fragment")]
float4 temporalMain(VSOutput input) : SV_Target0
{
    const int2 pixel = int2(input.position.xy);
    if (!litVoxelAt(pixel))
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    const bool specular = gPush.params.x > 0.5;
    const float3 current = noisyAt(pixel, specular);

    float3 mean = float3(0.0, 0.0, 0.0);
    float3 meanSquared = float3(0.0, 0.0, 0.0);
    float count = 0.0;
    const int2 size = int2(gPush.viewport.xy);
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const int2 wanted = pixel + int2(x, y);
            const int2 tap = clamp(wanted, int2(0, 0), size - 1);
            const float weight = all(tap == wanted) && litVoxelAt(tap) ? 1.0 : 0.0;
            const float3 value = noisyAt(tap, specular);
            mean += value * weight;
            meanSquared += value * value * weight;
            count += weight;
        }
    }
    mean /= count;
    const float3 sigma = sqrt(max(meanSquared / count - mean * mean, float3(0.0, 0.0, 0.0)));

    const float depth = gDepth.Load(int3(pixel, 0)).r;
    const float2 uv = (float2(pixel) + 0.5) / gPush.viewport.xy;
    const float linearDepth = linearDepthAt(uv, depth);
    const float4 reset = float4(current, specular ? 1.0 : linearDepth);

    // Camera reprojection when voxel motion is missing
    const float4 motion = gMotion.Load(int3(pixel, 0));
    float2 previousUv;
    float expectedDepth;
    if (motion.w > 0.5)
    {
        previousUv = uv - motion.xy;
        expectedDepth = linearDepth;
    }
    else
    {
        const float3 world = worldPositionAt(uv, depth);
        const float4 previousClip = mul(gPush.previousViewProjection, float4(world, 1.0));
        if (previousClip.w <= 0.0)
        {
            return reset;
        }
        previousUv = previousClip.xy / previousClip.w * 0.5 + 0.5;
        expectedDepth = previousClip.w;
    }

    const bool inside = all(previousUv >= float2(0.0, 0.0)) && all(previousUv <= float2(1.0, 1.0));

    // Reactive pixels drop their history
    if (gPush.params.y < 0.5 || !inside || motion.z > 0.5)
    {
        return reset;
    }

    // Depth lives in the diffuse history
    float4 diffuseHistory;
    float4 specularHistory;
    if (!reprojectHistory(previousUv, expectedDepth, diffuseHistory, specularHistory))
    {
        return reset;
    }

    const float historyLength = min(max(specularHistory.a, 1.0) + 1.0, kMaxHistoryLength);
    const float4 history = specular ? specularHistory : diffuseHistory;
    const float clip = specular ? kSpecularClip : kDiffuseClip;
    const float3 clipped = clamp(history.rgb, mean - clip * sigma, mean + clip * sigma);
    const float blend = max(1.0 / historyLength, specular ? kSpecularBlend : kDiffuseBlend);
    return float4(lerp(clipped, current, blend), specular ? historyLength : linearDepth);
}

// Interleaved gradient noise
float noiseAt(float2 pixel)
{
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

/// Additive into the scene colour
[shader("fragment")]
float4 compositeMain(VSOutput input) : SV_Target0
{
    const int2 pixel = int2(input.position.xy);
    const float4 albedo = gAlbedo.Load(int3(pixel, 0));
    if (!isLitVoxel(albedo.a))
    {
        discard;
    }

    const float depth = gDepth.Load(int3(pixel, 0)).r;
    const float2 uv = (float2(pixel) + 0.5) / gPush.viewport.xy;
    const float3 center = worldPositionAt(uv, depth);
    const float3 normal = decodeFaceNormal(gFaceNormal.Load(int3(pixel, 0)));
    const float centerDepth = linearDepthAt(uv, depth);

    // Tangent plane distance keeps grazing faces
    const float planeTolerance = 0.01 * centerDepth + 1e-3;
    const float rotation = (noiseAt(input.position.xy) + gPush.params.z) * 6.28318531;
    // Wider while the history is short
    const float settled = saturate((gSpecularCurrent.Load(int3(pixel, 0)).a - 1.0) / kSettleFrames);
    const float radius = gPush.params.w * lerp(kFreshRadiusScale, 1.0, settled);
    const int2 size = int2(gPush.viewport.xy);

    float3 diffuse = gDiffuseCurrent.Load(int3(pixel, 0)).rgb;
    float weightSum = 1.0;
    float s, c;
    sincos(rotation, s, c);
    const float2 turn = float2(c, s) * radius;
    [unroll]
    for (uint i = 0u; i < kSpatialTaps; ++i)
    {
        const float2 offset = float2(kSpiral[i].x * turn.x - kSpiral[i].y * turn.y, kSpiral[i].x * turn.y + kSpiral[i].y * turn.x);
        const int2 wanted = int2(floor(input.position.xy + offset));
        const int2 tap = clamp(wanted, int2(0, 0), size - 1);
        const float tapDepth = gDepth.Load(int3(tap, 0)).r;
        const float4 tapFace = gFaceNormal.Load(int3(tap, 0));
        const float3 tapDiffuse = gDiffuseCurrent.Load(int3(tap, 0)).rgb;

        const float3 tapPosition = worldPositionAt((float2(tap) + 0.5) / gPush.viewport.xy, tapDepth);
        const float plane = abs(dot(normal, tapPosition - center));
        const float facing = saturate(dot(normal, decodeFaceNormal(tapFace)));
        const float facing2 = facing * facing;
        const float facing8 = facing2 * facing2 * facing2 * facing2;
        const float facing32 = facing8 * facing8 * facing8 * facing8;
        const bool valid = all(tap == wanted) && tapFace.a > 0.5;
        const float weight = valid ? exp(-plane / planeTolerance) * facing32 : 0.0;
        diffuse += tapDiffuse * weight;
        weightSum += weight;
    }
    diffuse /= weightSum;

    const float3 specular = gSpecularCurrent.Load(int3(pixel, 0)).rgb;
    return float4(albedo.rgb * diffuse + specular, 0.0);
}
