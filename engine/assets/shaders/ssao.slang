import fullscreen;

struct SsaoParams
{
    float4x4 viewProjection;
    float4x4 inverseViewProjection;
    // xy target size zw reciprocal
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

static const float kNoSurfaceDistance = 60000.0;

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

float hash(float2 p)
{
    return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}

float3 hemisphereSample(uint index, uint count, float rotation)
{
    float golden = 2.39996323;
    float angle = float(index) * golden + rotation;
    float radius = sqrt((float(index) + 0.5) / float(count));

    float2 disk = float2(cos(angle), sin(angle)) * radius;
    float height = sqrt(max(1.0 - dot(disk, disk), 0.0));
    return float3(disk, height);
}

[shader("fragment")]
float4 aoMain(VSOutput input) : SV_Target0
{
    float4 normalRoughness = gNormalRoughness.Sample(input.uv);

    if (dot(normalRoughness.xyz, normalRoughness.xyz) < 1e-4)
    {
        return float4(1.0, kNoSurfaceDistance, 0.0, 1.0);
    }

    float depth = gDepth.Sample(input.uv).r;
    if (depth >= 1.0)
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

    float rotation = hash(input.uv * gParams.screenSize.xy) * 6.2831853;

    float occlusion = 0.0;
    float validSamples = 0.0;

    for (uint i = 0; i < sampleCount; ++i)
    {
        float3 hemisphere = hemisphereSample(i, sampleCount, rotation);
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
        if (sceneDepth >= 1.0)
        {
            validSamples += 1.0;
            continue;
        }

        float3 scenePosition = worldPositionFromDepth(sampleUv, sceneDepth);

        float3 toSample = scenePosition - worldPosition;
        float distanceToScene = length(toSample);

        float rangeWeight = saturate(1.0 - distanceToScene / max(rangeCutoff, 1e-4));

        if (distanceToScene > 1e-5)
        {
            float3 direction = toSample / distanceToScene;
            float facing = saturate(dot(N, direction));
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

float upsampleOcclusion(float2 uv, float pixelDistance, float tolerance)
{
    uint width;
    uint height;
    gOcclusion.GetDimensions(width, height);
    float2 lowSize = float2(float(width), float(height));

    float2 texel = uv * lowSize - 0.5;
    float2 base = floor(texel);
    float2 fraction = texel - base;

    float weighted = 0.0;
    float weightSum = 0.0;
    float nearestAo = 1.0;
    float nearestError = 1e30;

    for (int y = 0; y < 2; ++y)
    {
        for (int x = 0; x < 2; ++x)
        {
            float2 offset = float2(float(x), float(y));
            float4 texelValue = gOcclusion.SampleLevel((base + offset + 0.5) / lowSize, 0.0);

            float2 bilinear = lerp(1.0 - fraction, fraction, offset);
            float error = abs(texelValue.g - pixelDistance);
            float weight = bilinear.x * bilinear.y * exp(-error / tolerance);

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
    float3 sceneColor = gSceneColor.Sample(input.uv).rgb;
    float4 normalRoughness = gNormalRoughness.Sample(input.uv);

    uint renderMode = uint(gParams.misc.x);
    bool aoOnly = (renderMode == 15);

    // fwidth before any early return
    float depth = gDepth.Sample(input.uv).r;
    float3 worldPosition = worldPositionFromDepth(input.uv, depth);
    float pixelDistance = length(worldPosition - gParams.misc.yzw);
    float tolerance = clamp(2.0 * fwidth(pixelDistance), pixelDistance * 0.01, pixelDistance * 0.25);

    if (dot(normalRoughness.xyz, normalRoughness.xyz) < 1e-4)
    {
        return float4(aoOnly ? float3(1.0, 1.0, 1.0) : sceneColor, 1.0);
    }

    if (depth >= 1.0)
    {
        return float4(aoOnly ? float3(1.0, 1.0, 1.0) : sceneColor, 1.0);
    }

    float ao = upsampleOcclusion(input.uv, pixelDistance, tolerance);

    if (aoOnly)
    {
        return float4(ao, ao, ao, 1.0);
    }

    // Clamped since blended surfaces scaled the opaque term by 1 - alpha
    float3 indirect = gIndirectDiffuse.Sample(input.uv).rgb;
    return float4(max(sceneColor - indirect + indirect * ao, float3(0.0, 0.0, 0.0)), 1.0);
}
