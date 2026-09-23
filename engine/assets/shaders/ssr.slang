import fullscreen;

struct SsrParams
{
    float4x4 viewProjection;
    float4x4 inverseViewProjection;
    float4 cameraPosition;
    // x intensity y max roughness z thickness w step count
    float4 tuning;
    // x stride y render mode
    float4 marching;
};

[[vk::push_constant]]
ConstantBuffer<SsrParams> gParams;

[[vk::binding(0, 0)]]
Sampler2D gSceneColor;

[[vk::binding(1, 0)]]
Sampler2D gDepth;

[[vk::binding(2, 0)]]
Sampler2D gNormalRoughness;

// r hit distance g 1 on hit a camera distance
[[vk::binding(3, 0)]]
Sampler2D gReflection;

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

bool projectToScreen(float3 worldPosition, out float2 uv, out float depth)
{
    float4 clip = mul(gParams.viewProjection, float4(worldPosition, 1.0));
    if (clip.w <= 0.0)
    {
        uv = float2(0.0, 0.0);
        depth = 0.0;
        return false;
    }

    float3 ndc = clip.xyz / clip.w;
    uv = ndc.xy * 0.5 + 0.5;
    depth = ndc.z;
    return all(uv >= float2(0.0, 0.0)) && all(uv <= float2(1.0, 1.0));
}

float screenEdgeFade(float2 uv)
{
    float2 fromEdge = min(uv, float2(1.0, 1.0) - uv);
    float fade = saturate(min(fromEdge.x, fromEdge.y) / 0.1);
    return fade * fade;
}

float behindSurface(float3 position, float2 uv, float sceneDepth)
{
    float3 scenePosition = worldPositionFromDepth(uv, sceneDepth);
    return dot(position - scenePosition, normalize(scenePosition - gParams.cameraPosition.xyz));
}

[shader("fragment")]
float4 traceMain(VSOutput input) : SV_Target0
{
    float4 normalRoughness = gNormalRoughness.Sample(input.uv);

    if (dot(normalRoughness.xyz, normalRoughness.xyz) < 1e-4)
    {
        return float4(0.0, 0.0, 0.0, kNoSurfaceDistance);
    }

    float depth = gDepth.Sample(input.uv).r;
    if (depth >= 1.0)
    {
        return float4(0.0, 0.0, 0.0, kNoSurfaceDistance);
    }

    float3 worldPosition = worldPositionFromDepth(input.uv, depth);
    float4 nothing = float4(0.0, 0.0, 0.0, length(gParams.cameraPosition.xyz - worldPosition));

    float3 N = normalize(normalRoughness.xyz);

    if (normalRoughness.w > gParams.tuning.y)
    {
        return nothing;
    }

    float3 V = normalize(gParams.cameraPosition.xyz - worldPosition);
    float3 R = reflect(-V, N);

    if (dot(R, V) > 0.999)
    {
        return nothing;
    }

    uint stepCount = uint(gParams.tuning.w);
    float stride = gParams.marching.x;
    float thickness = gParams.tuning.z;

    // compositeMain starts from the same offset
    float3 rayStart = worldPosition + N * stride * 0.5;
    float3 rayPosition = rayStart;
    float3 previousPosition = rayStart;

    for (uint i = 0; i < stepCount; ++i)
    {
        rayPosition += R * stride;

        float2 sampleUv;
        float sampleDepth;
        if (!projectToScreen(rayPosition, sampleUv, sampleDepth))
        {
            break;
        }

        float sceneDepth = gDepth.SampleLevel(sampleUv, 0.0).r;
        if (sceneDepth >= 1.0)
        {
            previousPosition = rayPosition;
            continue;
        }

        float delta = behindSurface(rayPosition, sampleUv, sceneDepth);
        if (delta > 0.0)
        {
            if (delta > thickness)
            {
                break;
            }

            float3 lo = previousPosition;
            float3 hi = rayPosition;
            for (uint j = 0; j < 4; ++j)
            {
                float3 mid = (lo + hi) * 0.5;
                float2 midUv;
                float midDepth;
                if (!projectToScreen(mid, midUv, midDepth))
                {
                    break;
                }

                if (behindSurface(mid, midUv, gDepth.SampleLevel(midUv, 0.0).r) > 0.0)
                {
                    hi = mid;
                }
                else
                {
                    lo = mid;
                }
            }

            return float4(dot(hi - rayStart, R), 1.0, 0.0, nothing.a);
        }

        previousPosition = rayPosition;
    }

    return nothing;
}

bool resolveHit(float2 uv, float3 worldPosition, float3 N, float3 R, float pixelDistance, float tolerance,
                out float2 hitUv, out float coverage)
{
    hitUv = float2(0.0, 0.0);
    coverage = 0.0;

    uint width;
    uint height;
    gReflection.GetDimensions(width, height);
    float2 lowSize = float2(float(width), float(height));

    float2 texel = uv * lowSize - 0.5;
    float2 base = floor(texel);
    float2 fraction = texel - base;

    float3 rayStart = worldPosition + N * gParams.marching.x * 0.5;

    float bilateralSum = 0.0;
    float hitSum = 0.0;
    float bestError = 1e30;
    bool found = false;

    for (int y = 0; y < 2; ++y)
    {
        for (int x = 0; x < 2; ++x)
        {
            float2 offset = float2(float(x), float(y));
            float4 texelValue = gReflection.SampleLevel((base + offset + 0.5) / lowSize, 0.0);

            float2 bilinear = lerp(1.0 - fraction, fraction, offset);
            float bilateral = bilinear.x * bilinear.y * exp(-abs(texelValue.a - pixelDistance) / tolerance);
            bilateralSum += bilateral;
            hitSum += bilateral * texelValue.g;

            if (texelValue.g <= 0.0)
            {
                continue;
            }

            float3 candidate = rayStart + R * texelValue.r;
            float2 candidateUv;
            float candidateDepth;
            if (!projectToScreen(candidate, candidateUv, candidateDepth))
            {
                continue;
            }

            float sceneDepth = gDepth.SampleLevel(candidateUv, 0.0).r;
            if (sceneDepth >= 1.0)
            {
                continue;
            }

            float error = abs(behindSurface(candidate, candidateUv, sceneDepth));
            if (error < bestError)
            {
                bestError = error;
                hitUv = candidateUv;
                found = true;
            }
        }
    }

    if (!found || bestError > gParams.tuning.z)
    {
        return false;
    }

    coverage = bilateralSum > 1e-4 ? hitSum / bilateralSum : 1.0;
    return coverage > 0.0;
}

[shader("fragment")]
float4 compositeMain(VSOutput input) : SV_Target0
{
    float3 sceneColor = gSceneColor.Sample(input.uv).rgb;
    float4 normalRoughness = gNormalRoughness.Sample(input.uv);
    float depth = gDepth.Sample(input.uv).r;

    // fwidth before any early return
    float3 worldPosition = worldPositionFromDepth(input.uv, depth);
    float pixelDistance = length(gParams.cameraPosition.xyz - worldPosition);
    float tolerance = clamp(2.0 * fwidth(pixelDistance), pixelDistance * 0.01, pixelDistance * 0.25);

    uint renderMode = uint(gParams.marching.y);

    if (renderMode == 12)
    {
        if (dot(normalRoughness.xyz, normalRoughness.xyz) < 1e-4)
        {
            return float4(0.0, 0.0, 0.0, 1.0);
        }
        return float4(normalize(normalRoughness.xyz) * 0.5 + 0.5, 1.0);
    }

    if (renderMode == 13)
    {
        return float4(normalRoughness.www, 1.0);
    }

    // Voxel debug views 20 to 24 so a floor never reflects a legend colour
    if (renderMode >= 20 && renderMode <= 24)
    {
        return float4(sceneColor, 1.0);
    }

    bool ssrOnly = (renderMode == 14);
    float3 passthrough = ssrOnly ? float3(0.0, 0.0, 0.0) : sceneColor;

    if (dot(normalRoughness.xyz, normalRoughness.xyz) < 1e-4 || depth >= 1.0)
    {
        return float4(passthrough, 1.0);
    }

    float3 N = normalize(normalRoughness.xyz);
    float roughness = normalRoughness.w;
    float maxRoughness = gParams.tuning.y;
    if (roughness > maxRoughness)
    {
        return float4(passthrough, 1.0);
    }

    float3 V = normalize(gParams.cameraPosition.xyz - worldPosition);
    float3 R = reflect(-V, N);
    if (dot(R, V) > 0.999)
    {
        return float4(passthrough, 1.0);
    }

    float2 hitUv;
    float coverage;
    if (!resolveHit(input.uv, worldPosition, N, R, pixelDistance, tolerance, hitUv, coverage))
    {
        return float4(passthrough, 1.0);
    }

    float3 hitNormal = gNormalRoughness.SampleLevel(hitUv, 0.0).xyz;
    float facing = dot(hitNormal, hitNormal) > 1e-4 ? saturate(-dot(normalize(hitNormal), R)) : 0.0;

    float confidence = coverage * screenEdgeFade(hitUv) * step(0.01, facing);
    if (confidence <= 0.0)
    {
        return float4(passthrough, 1.0);
    }

    float3 hitColor = gSceneColor.SampleLevel(hitUv, 0.0).rgb;

    if (ssrOnly)
    {
        return float4(hitColor * confidence, 1.0);
    }

    float NdotV = saturate(dot(N, V));
    float fresnel = 0.04 + 0.96 * pow(1.0 - NdotV, 5.0);

    float roughnessWeight = saturate(1.0 - roughness / max(maxRoughness, 1e-4));

    float weight = confidence * fresnel * roughnessWeight * gParams.tuning.x;

    return float4(sceneColor + hitColor * weight, 1.0);
}
