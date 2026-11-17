import fullscreen;
import depth_convention;

struct SsaoParams
{
    float4x4 viewProjection;
    float4x4 inverseViewProjection;
    // xy target size z frame index for the noise w 1 when gDenoisedIndirect holds this frame's voxel GI
    float4 screenSize;
    // x sample radius y strength z depth cutoff w sample count
    float4 tuning;
    // x render mode yzw camera position
    float4 misc;
};

[[vk::push_constant]]
ConstantBuffer<SsaoParams> gParams;

[[vk::binding(0, 0)]]
Sampler2D gSceneColor;

[[vk::binding(1, 0)]]
Sampler2D gDepth;

[[vk::binding(2, 0)]]
Sampler2D gNormalRoughness;

[[vk::binding(3, 0)]]
Sampler2D gIndirectDiffuse;

// r occlusion g camera distance
[[vk::binding(4, 0)]]
Sampler2D gOcclusion;

/// Denoised voxel GI with alpha 1 where the voxel denoise composite wrote it
[[vk::binding(5, 0)]]
Sampler2D gDenoisedIndirect;

static const float kNoSurfaceDistance = 60000.0;

/// Share of the radius the closest samples sit at
static const float kNearestSample = 0.1;

/// Cosine an occluder has to rise above the tangent plane so depth steps on flat or sloped ground do not occlude
static const float kAngleBias = 0.15;

typedef FullscreenVSOutput VSOutput;

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VertexID)
{
    return fullscreenTriangle(vertexID);
}

float3 worldPositionFromDepth(float2 uv, float depth)
{
    float4 clip = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 world = mul(gParams.inverseViewProjection, clip);
    return world.xyz / world.w;
}

/// Walked per frame so TAA averages it
float rotationNoise(float2 pixel)
{
    const float2 shifted = pixel + 5.588238 * gParams.screenSize.z;
    return frac(52.9829189 * frac(dot(shifted, float2(0.06711056, 0.00583715))));
}

/// Cosine weighted direction scaled so samples crowd near the surface where contact shadows are
float3 hemisphereSample(uint index, uint count, float rotation, float shift)
{
    float golden = 2.39996323;
    float angle = float(index) * golden + rotation;
    float radius = sqrt((float(index) + 0.5) / float(count));

    float s, c;
    sincos(angle, s, c);
    float2 disk = float2(c, s) * radius;
    float height = sqrt(max(1.0 - dot(disk, disk), 0.0));
    // A unit direction alone puts every sample a full radius out and misses creases closer than that
    const float along = frac((float(index) + shift) / float(count) * 7.0 + shift);
    return float3(disk, height) * lerp(kNearestSample, 1.0, along * along);
}

[shader("fragment")]
float4 aoMain(VSOutput input) : SV_Target0
{
    float depth = gDepth.Sample(input.uv).r;
    if (isFarDepth(depth))
    {
        return float4(1.0, kNoSurfaceDistance, 0.0, 1.0);
    }

    float4 normalRoughness = gNormalRoughness.Sample(input.uv);
    if (dot(normalRoughness.xyz, normalRoughness.xyz) < 1e-4)
    {
        return float4(1.0, kNoSurfaceDistance, 0.0, 1.0);
    }

    float3 N = normalize(normalRoughness.xyz);
    float3 worldPosition = worldPositionFromDepth(input.uv, depth);
    float surfaceDistance = length(worldPosition - gParams.misc.yzw);

    float radius = gParams.tuning.x;
    float strength = gParams.tuning.y;
    float rangeCutoff = gParams.tuning.z;
    uint sampleCount = uint(gParams.tuning.w);

    float3 helper = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 T = normalize(cross(helper, N));
    float3 B = cross(N, T);

    const float noise = rotationNoise(input.position.xy);
    float rotation = noise * 6.2831853;

    float occlusion = 0.0;
    float validSamples = 0.0;

    for (uint i = 0; i < sampleCount; ++i)
    {
        float3 hemisphere = hemisphereSample(i, sampleCount, rotation, noise);
        float3 offset = (T * hemisphere.x + B * hemisphere.y + N * hemisphere.z) * radius;

        float3 samplePosition = worldPosition + offset;

        float4 projected = mul(gParams.viewProjection, float4(samplePosition, 1.0));
        if (projected.w <= 0.0)
        {
            continue;
        }

        float3 ndc = projected.xyz / projected.w;
        float2 sampleUv = ndc.xy * 0.5 + 0.5;
        if (any(sampleUv < float2(0.0, 0.0)) || any(sampleUv > float2(1.0, 1.0)))
        {
            continue;
        }

        float sceneDepth = gDepth.SampleLevel(sampleUv, 0.0).r;
        if (isFarDepth(sceneDepth))
        {
            validSamples += 1.0;
            continue;
        }

        float3 scenePosition = worldPositionFromDepth(sampleUv, sceneDepth);

        float3 toSample = scenePosition - worldPosition;
        float distanceToScene = length(toSample);

        // Smooth so an occluder near the cutoff fades instead of popping and close ones weigh most
        const float range = distanceToScene / max(rangeCutoff, 1e-4);
        float rangeWeight = saturate(1.0 - range * range);

        if (distanceToScene > 1e-5)
        {
            float3 direction = toSample / distanceToScene;
            float facing = saturate((dot(N, direction) - kAngleBias) / (1.0 - kAngleBias));
            occlusion += facing * rangeWeight;
        }

        validSamples += 1.0;
    }

    float ao = 1.0;
    if (validSamples > 0.0)
    {
        ao = saturate(1.0 - (occlusion / validSamples) * strength);
    }

    return float4(ao, surfaceDistance, 0.0, 1.0);
}

/// 4 by 4 texels kept on the surface by camera distance
float upsampleOcclusion(float2 uv, float pixelDistance, float tolerance)
{
    uint width;
    uint height;
    gOcclusion.GetDimensions(width, height);
    float2 lowSize = float2(float(width), float(height));

    float2 texel = uv * lowSize - 0.5;
    float2 base = floor(texel);

    float weighted = 0.0;
    float weightSum = 0.0;
    float nearestAo = 1.0;
    float nearestError = 1e30;

    for (int y = -1; y <= 2; ++y)
    {
        for (int x = -1; x <= 2; ++x)
        {
            float2 offset = float2(float(x), float(y));
            float4 texelValue = gOcclusion.SampleLevel((base + offset + 0.5) / lowSize, 0.0);

            float2 fromCentre = base + offset - texel;
            float error = abs(texelValue.g - pixelDistance);
            float weight = exp(-dot(fromCentre, fromCentre) * 0.5 - error / tolerance);

            weighted += texelValue.r * weight;
            weightSum += weight;

            if (error < nearestError)
            {
                nearestError = error;
                nearestAo = texelValue.r;
            }
        }
    }

    return weightSum > 1e-4 ? weighted / weightSum : nearestAo;
}

[shader("fragment")]
float4 compositeMain(VSOutput input) : SV_Target0
{
    float depth = gDepth.Sample(input.uv).r;
    uint renderMode = uint(gParams.misc.x);
    bool aoOnly = (renderMode == 15);

    if (isFarDepth(depth))
    {
        float3 sceneColor = aoOnly ? float3(1.0, 1.0, 1.0) : gSceneColor.Sample(input.uv).rgb;
        return float4(sceneColor, 1.0);
    }

    float4 normalRoughness = gNormalRoughness.Sample(input.uv);
    if (dot(normalRoughness.xyz, normalRoughness.xyz) < 1e-4)
    {
        float3 sceneColor = aoOnly ? float3(1.0, 1.0, 1.0) : gSceneColor.Sample(input.uv).rgb;
        return float4(sceneColor, 1.0);
    }

    float3 worldPosition = worldPositionFromDepth(input.uv, depth);
    float pixelDistance = length(worldPosition - gParams.misc.yzw);
    float tolerance = clamp(2.0 * fwidth(pixelDistance), pixelDistance * 0.01, pixelDistance * 0.25);

    float ao = upsampleOcclusion(input.uv, pixelDistance, tolerance);

    if (aoOnly)
    {
        return float4(ao, ao, ao, 1.0);
    }

    float3 sceneColor = gSceneColor.Sample(input.uv).rgb;
    // Clamped since blended surfaces scaled the opaque term by 1 - alpha
    float3 indirect = gIndirectDiffuse.Sample(input.uv).rgb;
    // The colour holds the denoised voxel GI so the share taken back must be that one and not this frame's noisy one
    if (gParams.screenSize.w > 0.5)
    {
        const float4 denoised = gDenoisedIndirect.Sample(input.uv);
        indirect = denoised.a > 0.5 ? denoised.rgb : indirect;
    }
    return float4(max(sceneColor - indirect + indirect * ao, float3(0.0, 0.0, 0.0)), 1.0);
}
