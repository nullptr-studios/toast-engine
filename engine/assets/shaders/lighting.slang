// Set 0 belongs to the engine and importing shaders own set 1

struct DirectionalLightGpu
{
    float4 direction;
    float4 colorIntensity;
};

// Mirrors VulkanRenderer::IrradianceVolumeData
struct IrradianceVolumeGpu
{
    // xyz grid min corner w nominal spacing
    float4 minCornerSpacing;

    // xyz probe counts w first probe index in gIrradianceSH
    uint4 countsBase;

    // xyz half extents w intensity
    float4 extentsIntensity;

    // x 1 once baked for the current grid
    uint4 bakedPad;
};

// Mirrors VulkanRenderer::ReflectionProbeData
struct ReflectionProbeGpu
{
    // xyz position w influence radius
    float4 positionRadius;

    // xyz box half extents w intensity Zero extents select sphere projection
    float4 boxExtentsIntensity;

    // x cubemap index in gProbeMaps and gProbeIrradiance
    float4 params;
};

struct CameraUBO
{
    float4x4 view;
    float4x4 projection;
    float4x4 viewProjection;

    float3 position;
    float time;

    float4 ambientColorIntensity;
    uint4 directionalLightCountPad;
    DirectionalLightGpu directionalLights[4];
    uint4 renderModePad;

    // Must match renderer::shadows::k_cascade_count
    float4x4 cascadeViewProjection[4];

    float4 cascadeSplits;
    float4 cascadeTexelWorldSize;
    float4 cascadeDepthBias;

    // x caster index + 1 (0 none) y cascade count z cascade texel in UV w punctual texel in UV
    float4 shadowParams;

    // x 1 when the cubemaps hold data y gPrefilteredEnv roughness levels
    float4 environmentParams;

    // x probe count y probe roughness levels z baked bit per probe
    uint4 reflectionProbeCountPad;

    // Must match VulkanRenderer::k_max_reflection_probes
    ReflectionProbeGpu reflectionProbes[8];

    uint4 irradianceVolumeCountPad;

    // Must match VulkanRenderer::k_max_irradiance_volumes
    IrradianceVolumeGpu irradianceVolumes[4];

    // x 1 when gScene holds a TLAS and traced shadows are on. Keep last
    float4 tracedShadowParams;
};

[[vk::binding(0,0)]]
ConstantBuffer<CameraUBO> gCamera;

struct ClusterParams
{
    float4x4 inverseProjection;
    uint4 clusterDims;         // xyz dims w max lights per cluster
    float4 screenSizeNearFar;  // xy screen size z near w far
    uint lightCount;
    float3 _pad0;
};

struct GpuLight
{
    float4 worldPosRange;
    float4 viewPosType;     // w type 0 point 1 spot
    float4 colorIntensity;
    float4 directionPad;    // w shadow layer or -1
    float4 coneAngles;      // x cos outer y cos inner z shadow near w shadow far

    // x fraction of the shadow layer rendered
    float4 shadowAtlas;

    // Spotlight only
    float4x4 shadowViewProjection;
};

[[vk::binding(1,0)]]
ConstantBuffer<ClusterParams> gClusterParams;
[[vk::binding(2,0)]]
StructuredBuffer<GpuLight> gLights;
[[vk::binding(3,0)]]
StructuredBuffer<uint> gClusterLightGrid;
[[vk::binding(4,0)]]
StructuredBuffer<uint> gLightIndexList;

[[vk::binding(6,0)]]
Sampler2DArrayShadow gShadowMap;

[[vk::binding(7,0)]]
Sampler2DArrayShadow gPunctualShadowMaps;

[[vk::binding(8,0)]]
SamplerCube gIrradianceMap;

[[vk::binding(9,0)]]
SamplerCube gPrefilteredEnv;

// Descriptor array not a cube array since imageCubeArray is not enabled
[[vk::binding(10,0)]]
SamplerCube gProbeMaps[8];

[[vk::binding(11,0)]]
SamplerCube gProbeIrradiance[8];

// Four float4 per probe L0 L1y L1z L1x
[[vk::binding(12,0)]]
StructuredBuffer<float4> gIrradianceSH;

// Ray query only Gated on tracedShadowParams.x
#if TOAST_RAY_QUERY
[[vk::binding(14,0)]]
RaytracingAccelerationStructure gScene;
#endif

static const float kPi = 3.14159265359;

float distributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = kPi * denom * denom;
    return a2 / max(denom, 0.0000001);
}

float geometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float denom = NdotV * (1.0 - k) + k;
    return NdotV / max(denom, 0.0000001);
}

float geometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggxV = geometrySchlickGGX(NdotV, roughness);
    float ggxL = geometrySchlickGGX(NdotL, roughness);
    return ggxV * ggxL;
}

float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (float3(1.0, 1.0, 1.0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    float3 smoothness = float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness);
    return F0 + (max(smoothness, F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float3 parallaxCorrectedReflection(
    float3 reflection, float3 worldPosition, float3 probePosition, float3 probeExtents, float radius)
{
    float3 toSurface = worldPosition - probePosition;

    if (all(probeExtents <= float3(0.0, 0.0, 0.0)))
    {
        float b = dot(toSurface, reflection);
        float c = dot(toSurface, toSurface) - radius * radius;
        float discriminant = (b * b) - c;
        if (discriminant <= 0.0)
        {
            return reflection;
        }
        float t = -b + sqrt(discriminant);
        return normalize(toSurface + reflection * t);
    }

    float3 firstPlane = (probeExtents - toSurface) / reflection;
    float3 secondPlane = (-probeExtents - toSurface) / reflection;
    float3 furthest = max(firstPlane, secondPlane);
    float distance = min(min(furthest.x, furthest.y), furthest.z);

    return normalize(toSurface + reflection * distance);
}

float reflectionProbeWeight(float3 worldPosition, ReflectionProbeGpu probe)
{
    float radius = probe.positionRadius.w;
    if (radius <= 0.0)
    {
        return 0.0;
    }

    float3 local = worldPosition - probe.positionRadius.xyz;
    float3 extents = probe.boxExtentsIntensity.xyz;

    if (any(extents > float3(0.0, 0.0, 0.0)))
    {
        if (any(abs(local) > extents))
        {
            return 0.0;
        }

        float3 margin = max(extents * 0.25, float3(1e-4, 1e-4, 1e-4));
        float3 axisWeight = saturate((extents - abs(local)) / margin);
        float weight = min(min(axisWeight.x, axisWeight.y), axisWeight.z);
        return weight * weight;
    }

    float normalized = saturate(1.0 - (length(local) / radius));
    return normalized * normalized;
}

float3 evaluateIrradianceSH(uint probeIndex, float3 normal)
{
    uint base = probeIndex * 4;
    float3 l0 = gIrradianceSH[base + 0].rgb;
    float3 l1y = gIrradianceSH[base + 1].rgb;
    float3 l1z = gIrradianceSH[base + 2].rgb;
    float3 l1x = gIrradianceSH[base + 3].rgb;

    // 0.282095 = Y00 and 0.325735 = (2/3) * 0.488603
    float3 result = 0.282095 * l0 + 0.325735 * (l1x * normal.x + l1y * normal.y + l1z * normal.z);

    return max(result, float3(0.0, 0.0, 0.0));
}

bool sampleIrradianceVolumes(float3 worldPosition, float3 normal, out float3 irradiance)
{
    irradiance = float3(0.0, 0.0, 0.0);

    for (uint v = 0; v < gCamera.irradianceVolumeCountPad.x; ++v)
    {
        IrradianceVolumeGpu volume = gCamera.irradianceVolumes[v];

        if (volume.bakedPad.x == 0)
        {
            continue;
        }

        uint3 counts = volume.countsBase.xyz;
        if (any(counts < uint3(2, 2, 2)))
        {
            continue;
        }

        float3 minCorner = volume.minCornerSpacing.xyz;
        float3 extents = volume.extentsIntensity.xyz;
        float3 step = (extents * 2.0) / max(float3(counts) - 1.0, float3(1.0, 1.0, 1.0));

        float3 samplePosition = worldPosition + normal * (min(min(step.x, step.y), step.z) * 0.2);

        float3 local = (samplePosition - minCorner) / step;
        if (any(local < float3(-0.5, -0.5, -0.5)) || any(local > float3(counts) - 0.5))
        {
            continue;
        }

        float3 clamped = clamp(local, float3(0.0, 0.0, 0.0), float3(counts) - 1.0001);
        int3 baseCell = int3(floor(clamped));
        float3 frac3 = clamped - float3(baseCell);

        float3 accumulated = float3(0.0, 0.0, 0.0);
        float totalWeight = 0.0;

        for (uint corner = 0; corner < 8; ++corner)
        {
            int3 offset = int3(int(corner & 1u), int((corner >> 1u) & 1u), int((corner >> 2u) & 1u));
            int3 cell = min(baseCell + offset, int3(counts) - 1);

            float3 blend = lerp(1.0 - frac3, frac3, float3(offset));
            float weight = blend.x * blend.y * blend.z;
            if (weight <= 0.0)
            {
                continue;
            }

            uint probeIndex = volume.countsBase.w + uint(cell.x) + uint(cell.y) * counts.x + uint(cell.z) * counts.x * counts.y;

            float3 probePosition = minCorner + float3(cell) * step;
            float3 toProbe = probePosition - worldPosition;
            float distanceToProbe = length(toProbe);

            float facing = distanceToProbe > 1e-4 ? saturate(dot(normal, toProbe / distanceToProbe) * 0.5 + 0.5) : 1.0;

            weight *= facing * facing;
            if (weight <= 0.0)
            {
                continue;
            }

            accumulated += evaluateIrradianceSH(probeIndex, normal) * weight;
            totalWeight += weight;
        }

        if (totalWeight > 0.0)
        {
            irradiance = (accumulated / totalWeight) * volume.extentsIntensity.w;
            return true;
        }
    }

    return false;
}

float2 environmentBRDF(float roughness, float NdotV)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}

float3 shadeLight(float3 N, float3 V, float3 L, float3 radiance, float3 albedo, float metallic, float roughness)
{
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    float3 H = normalize(V + L);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    float3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
    float3 specular = numerator / denominator;

    float3 kD = (float3(1.0, 1.0, 1.0) - F) * (1.0 - metallic);

    return (kD * albedo / kPi + specular) * radiance * NdotL;
}

float attenuateRange(float dist, float range)
{
    float rangeFalloff = clamp(1.0 - pow(dist / max(range, 0.0001), 4.0), 0.0, 1.0);
    rangeFalloff = rangeFalloff * rangeFalloff;
    return rangeFalloff / max(dist * dist, 0.0001);
}

static const float kShadowFilterRadiusTexels = 2.5;

static const float kShadowNormalOffsetTexels = 0.5;

static const float kMaxShadowContactOffset = 0.02;

float shadowNormalOffset(float texelWorldSize, float NdotL)
{
    float sinTheta = sqrt(saturate(1.0 - NdotL * NdotL));
    float offset = texelWorldSize * kShadowNormalOffsetTexels * sinTheta;

    float lateral = offset * sinTheta / max(NdotL, 1e-3);
    if (lateral > kMaxShadowContactOffset)
    {
        offset *= kMaxShadowContactOffset / lateral;
    }

    return offset;
}

static const float2 kPoissonDisk[12] = {
    float2(-0.326, -0.406), float2(-0.840, -0.074), float2(-0.696,  0.457), float2(-0.203,  0.621),
    float2( 0.962, -0.195), float2( 0.473, -0.480), float2( 0.519,  0.767), float2( 0.185, -0.893),
    float2( 0.507,  0.064), float2( 0.896,  0.412), float2(-0.322, -0.933), float2(-0.792, -0.598),
};

float interleavedGradientNoise(float2 pixel)
{
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

// Screen space derivatives break across cascade seams hence the clamp at the call site
float2 receiverPlaneDepthGradient(float2 uv, float depth)
{
    float3 dx = float3(ddx(uv), ddx(depth));
    float3 dy = float3(ddy(uv), ddy(depth));

    float determinant = (dx.x * dy.y) - (dx.y * dy.x);
    if (abs(determinant) < 1e-12)
    {
        return float2(0.0, 0.0);
    }

    float inverse = 1.0 / determinant;
    return float2(
        ((dy.y * dx.z) - (dx.y * dy.z)) * inverse,
        ((dx.x * dy.z) - (dy.x * dx.z)) * inverse);
}

float pcfShadow(
    Sampler2DArrayShadow shadowMaps, float2 uv, float layer, float compareDepth, float texelSize, float2 pixel,
    float2 gradient, float maxGradientAdjust)
{
    if (any(uv < float2(0.0, 0.0)) || any(uv > float2(1.0, 1.0)) || compareDepth > 1.0 || compareDepth < 0.0)
    {
        return 1.0;
    }

    float angle = interleavedGradientNoise(pixel) * 6.28318530718;
    float2 rotation = float2(cos(angle), sin(angle));
    float radius = texelSize * kShadowFilterRadiusTexels;

    float sum = 0.0;
    for (int i = 0; i < 12; ++i)
    {
        float2 tap = kPoissonDisk[i];
        float2 offset = float2(
            tap.x * rotation.x - tap.y * rotation.y,
            tap.x * rotation.y + tap.y * rotation.x) * radius;

        float tapDepth = compareDepth + clamp(dot(gradient, offset), -maxGradientAdjust, maxGradientAdjust);

        // LevelZero since implicit derivatives are undefined under per pixel branches
        sum += shadowMaps.SampleCmpLevelZero(float3(uv + offset, layer), tapDepth);
    }

    return sum / 12.0;
}

float sampleCascade(uint cascade, float3 worldPosition, float3 N, float3 L, float2 pixel)
{
    float NdotL = saturate(dot(N, L));
    float3 offsetPosition = worldPosition + N * shadowNormalOffset(gCamera.cascadeTexelWorldSize[cascade], NdotL);

    float4 lightClip = mul(gCamera.cascadeViewProjection[cascade], float4(offsetPosition, 1.0));
    if (lightClip.w <= 0.0)
    {
        return 1.0;
    }

    float3 projected = lightClip.xyz / lightClip.w;
    float2 uv = projected.xy * 0.5 + 0.5;

    float bias = gCamera.cascadeDepthBias[cascade];
    float2 gradient = receiverPlaneDepthGradient(uv, projected.z);

    return pcfShadow(
        gShadowMap,
        uv,
        float(cascade),
        projected.z - bias,
        gCamera.shadowParams.z,
        pixel,
        gradient,
        bias * 8.0);
}

// Guards every gScene access. Folds to false without ray query
bool tracedShadowsEnabled()
{
#if TOAST_RAY_QUERY
    return gCamera.tracedShadowParams.x >= 0.5;
#else
    return false;
#endif
}

float traceShadowRay(float3 worldPosition, float3 N, float3 L, float maxDistance)
{
#if TOAST_RAY_QUERY
    RayDesc ray;
    ray.Origin = worldPosition + N * 0.02;
    ray.Direction = L;
    ray.TMin = 0.0;
    ray.TMax = max(maxDistance - 0.02, 0.0);

    RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(gScene, RAY_FLAG_NONE, 0xFF, ray);
    query.Proceed();

    return query.CommittedStatus() == COMMITTED_NOTHING ? 1.0 : 0.0;
#else
    return 1.0;
#endif
}

float sampleDirectionalShadow(float3 worldPosition, float3 N, float3 L, float viewDepth, float2 pixel)
{
    if (gCamera.shadowParams.x < 0.5)
    {
        return 1.0;
    }

    uint cascadeCount = uint(gCamera.shadowParams.y);
    uint cascade = cascadeCount;
    for (uint i = 0; i < cascadeCount; ++i)
    {
        if (viewDepth <= gCamera.cascadeSplits[i])
        {
            cascade = i;
            break;
        }
    }
    if (cascade >= cascadeCount)
    {
        return 1.0;
    }

    float shadow = sampleCascade(cascade, worldPosition, N, L, pixel);

    float cascadeFar = gCamera.cascadeSplits[cascade];
    float cascadeNear = cascade > 0 ? gCamera.cascadeSplits[cascade - 1] : 0.0;
    float span = max(cascadeFar - cascadeNear, 1e-4);
    float t = (viewDepth - cascadeNear) / span;

    const float kBlendBand = 0.15;
    if (t > (1.0 - kBlendBand) && (cascade + 1) < cascadeCount)
    {
        float nextShadow = sampleCascade(cascade + 1, worldPosition, N, L, pixel);
        shadow = lerp(shadow, nextShadow, (t - (1.0 - kBlendBand)) / kBlendBand);
    }

    return shadow;
}

float sampleSpotShadow(GpuLight light, float3 worldPosition, float3 N, float3 L, float2 pixel)
{
    float layer = light.directionPad.w;
    if (layer < 0.0)
    {
        return 1.0;
    }

    float NdotL = saturate(dot(N, L));
    float texelWorldSize =
        distance(worldPosition, light.worldPosRange.xyz) * (gCamera.shadowParams.w / max(light.shadowAtlas.x, 1e-4)) * 2.0;
    float3 offsetPosition = worldPosition + N * shadowNormalOffset(texelWorldSize, NdotL);

    float4 lightClip = mul(light.shadowViewProjection, float4(offsetPosition, 1.0));
    if (lightClip.w <= 0.0)
    {
        return 1.0;
    }

    float3 projected = lightClip.xyz / lightClip.w;
    float2 uv = (projected.xy * 0.5 + 0.5) * light.shadowAtlas.x;

    float bias = max(0.0005, 0.0025 * (1.0 - projected.z));
    float2 gradient = receiverPlaneDepthGradient(uv, projected.z);

    return pcfShadow(
        gPunctualShadowMaps, uv, layer, projected.z - bias, gCamera.shadowParams.w, pixel, gradient, bias * 8.0);
}

// Six layers in cubeFaceBasis() order +X -X +Y -Y +Z -Z
float samplePointShadow(GpuLight light, float3 worldPosition, float3 N, float3 L, float2 pixel)
{
    float baseLayer = light.directionPad.w;
    if (baseLayer < 0.0)
    {
        return 1.0;
    }

    float nearPlane = light.coneAngles.z;
    float farPlane = light.coneAngles.w;

    float NdotL = saturate(dot(N, L));
    float texelWorldSize =
        distance(worldPosition, light.worldPosRange.xyz) * (gCamera.shadowParams.w / max(light.shadowAtlas.x, 1e-4)) * 2.0;
    float3 offsetPosition = worldPosition + N * shadowNormalOffset(texelWorldSize, NdotL);

    float3 direction = offsetPosition - light.worldPosRange.xyz;
    float3 absDirection = abs(direction);
    float majorAxis = max(absDirection.x, max(absDirection.y, absDirection.z));
    if (majorAxis < 0.0001)
    {
        return 1.0;
    }

    // Must agree with cubeFaceBasis() or shadows tear at face seams
    uint face;
    float2 ndc;
    if (absDirection.x >= absDirection.y && absDirection.x >= absDirection.z)
    {
        face = direction.x > 0.0 ? 0 : 1;
        ndc = float2(direction.x > 0.0 ? -direction.y : direction.y, direction.z) / majorAxis;
    }
    else if (absDirection.y >= absDirection.z)
    {
        face = direction.y > 0.0 ? 2 : 3;
        ndc = float2(direction.y > 0.0 ? direction.x : -direction.x, direction.z) / majorAxis;
    }
    else
    {
        face = direction.z > 0.0 ? 4 : 5;
        ndc = float2(direction.z > 0.0 ? -direction.x : direction.x, direction.y) / majorAxis;
    }

    ndc *= light.shadowAtlas.y;

    float2 uv = (ndc * 0.5 + 0.5) * light.shadowAtlas.x;

    // perspectiveRH_ZO depth at majorAxis is z = f/(f-n) - f*n/((f-n)*majorAxis)
    float range = max(farPlane - nearPlane, 0.0001);
    float faceDepth = (farPlane / range) - ((farPlane * nearPlane) / (range * majorAxis));

    float bias = max(0.0005, 0.0025 * (1.0 - faceDepth));
    float2 gradient = receiverPlaneDepthGradient(uv, faceDepth);

    return pcfShadow(
        gPunctualShadowMaps, uv, baseLayer + float(face), faceDepth - bias, gCamera.shadowParams.w, pixel, gradient,
        bias * 8.0);
}

float3 clusterHeatmapColor(uint lightCount)
{
    const float kHeatmapMaxLights = 8.0;
    float t = clamp(float(lightCount) / kHeatmapMaxLights, 0.0, 1.0);

    float3 c0 = float3(0.0, 0.0, 1.0);
    float3 c1 = float3(0.0, 1.0, 0.0);
    float3 c2 = float3(1.0, 1.0, 0.0);
    float3 c3 = float3(1.0, 0.0, 0.0);

    if (t < 0.333) return lerp(c0, c1, t / 0.333);
    if (t < 0.667) return lerp(c1, c2, (t - 0.333) / 0.334);
    return lerp(c2, c3, (t - 0.667) / 0.333);
}

struct Surface
{
    float3 worldPosition;

    float3 normal;

    float3 geometricNormal;

    float3 viewDirection;

    float3 albedo;
    float  metallic;
    float  roughness;

    float  occlusion;

    float  viewSpaceZ;

    float2 pixel;
};

struct ShadingResult
{
    float3 color;

    float3 indirectDiffuse;

    bool isDebugView;
};

struct ClusterCell
{
    uint index;
    uint count;
    uint lightCount;

    float viewDistance;
};

/// Must match ClusterLightingPass i + j*dimX + k*dimX*dimY
ClusterCell clusterCellAt(float viewSpaceZ, float2 pixel)
{
    Surface surface;
    surface.viewSpaceZ = viewSpaceZ;
    surface.pixel = pixel;

    float dist = max(-surface.viewSpaceZ, gClusterParams.screenSizeNearFar.z);
    float near = gClusterParams.screenSizeNearFar.z;
    float far = gClusterParams.screenSizeNearFar.w;
    uint dimX = gClusterParams.clusterDims.x;
    uint dimY = gClusterParams.clusterDims.y;
    uint dimZ = gClusterParams.clusterDims.z;

    uint zSlice = uint(clamp(log(dist / near) * float(dimZ) / log(far / near), 0.0, float(dimZ - 1)));

    float2 tileSize = gClusterParams.screenSizeNearFar.xy / float2(dimX, dimY);
    uint2 tile = uint2(clamp(surface.pixel / tileSize, float2(0.0, 0.0), float2(dimX - 1, dimY - 1)));

    uint clusterIndex = tile.x + tile.y * dimX + zSlice * dimX * dimY;
    uint clusterCount = dimX * dimY * dimZ;

    uint lightCountInCluster = 0;
    if (clusterIndex < clusterCount)
    {
        lightCountInCluster = gClusterLightGrid[clusterIndex];
    }

    ClusterCell cell;
    cell.index = clusterIndex;
    cell.count = clusterCount;
    cell.lightCount = lightCountInCluster;
    cell.viewDistance = dist;
    return cell;
}

// Mirrored by renderer::voxel_debug::View and the editor RenderMode enum
static const uint kRenderModeVoxelSteps = 20;
static const uint kRenderModeVoxelTraversal = 21;
static const uint kRenderModeVoxelBricks = 22;
static const uint kRenderModeVoxelVolumes = 23;
static const uint kRenderModeVoxelMaterials = 24;

bool isVoxelDebugMode(uint mode)
{
    return mode >= kRenderModeVoxelSteps && mode <= kRenderModeVoxelMaterials;
}

float4 evaluateDebugView(Surface surface, ClusterCell cluster)
{
    float3 albedo = surface.albedo;
    float3 N = surface.normal;
    float3 V = surface.viewDirection;
    float metallic = surface.metallic;
    float roughness = surface.roughness;
    uint lightCountInCluster = cluster.lightCount;

    if (gCamera.renderModePad.x == 1)
    {
        return float4(clusterHeatmapColor(lightCountInCluster), 1.0);
    }

    // Neutral so no mesh colour reads as part of a voxel legend
    if (isVoxelDebugMode(gCamera.renderModePad.x))
    {
        return float4(float3(0.06, 0.06, 0.07) + 0.2 * saturate(dot(N, V)), 1.0);
    }

    switch (gCamera.renderModePad.x)
    {
    case 2:
        return float4(albedo, 1.0);
    case 3:
        return float4(N * 0.5 + 0.5, 1.0);
    case 4:
        return float4(metallic, roughness, 0.0, 1.0);
    case 5:
        return float4(
            gCamera.ambientColorIntensity.rgb * gCamera.ambientColorIntensity.a +
                (gCamera.environmentParams.x >= 0.5 ? gIrradianceMap.SampleLevel(N, 0.0).rgb : float3(0.0, 0.0, 0.0)),
            1.0);
    case 6:
        {
            float3 debugReflection = reflect(-V, N);
            float debugLod = roughness * max(gCamera.environmentParams.y - 1.0, 0.0);
            return float4(
                gCamera.environmentParams.x >= 0.5
                    ? gPrefilteredEnv.SampleLevel(debugReflection, debugLod).rgb
                    : float3(0.0, 0.0, 0.0),
                1.0);
        }
    case 7:
        {
            float3 baseReflection = reflect(-V, N);
            return float4(
                gCamera.environmentParams.x >= 0.5 ? gPrefilteredEnv.SampleLevel(baseReflection, 0.0).rgb
                                                   : float3(0.0, 0.0, 0.0),
                1.0);
        }
    case 8:
        return float4(reflect(-V, N) * 0.5 + 0.5, 1.0);
    case 9:
        {
            int debugProbe = -1;
            float debugWeight = 0.0;
            for (uint pi = 0; pi < gCamera.reflectionProbeCountPad.x; ++pi)
            {
                float weight = reflectionProbeWeight(surface.worldPosition, gCamera.reflectionProbes[pi]);
                if (weight > debugWeight)
                {
                    debugWeight = weight;
                    debugProbe = int(pi);
                }
            }

            if (debugProbe < 0)
            {
                return float4(0.05, 0.05, 0.05, 1.0);
            }

            float hue = frac(gCamera.reflectionProbes[debugProbe].params.x * 0.618033988);
            float3 tint = saturate(abs(frac(hue + float3(0.0, 0.6666, 0.3333)) * 6.0 - 3.0) - 1.0);
            return float4(tint * debugWeight, 1.0);
        }
    case 10:
        {
            int capProbe = -1;
            float capWeight = 0.0;
            for (uint pi = 0; pi < gCamera.reflectionProbeCountPad.x; ++pi)
            {
                float weight = reflectionProbeWeight(surface.worldPosition, gCamera.reflectionProbes[pi]);
                if (weight > capWeight)
                {
                    capWeight = weight;
                    capProbe = int(pi);
                }
            }

            if (capProbe < 0)
            {
                return float4(1.0, 0.0, 1.0, 1.0);
            }

            uint capCube = uint(gCamera.reflectionProbes[capProbe].params.x);
            if ((gCamera.reflectionProbeCountPad.z & (1u << capCube)) == 0u)
            {
                return float4(1.0, 1.0, 0.0, 1.0);
            }

            ReflectionProbeGpu capProbeData = gCamera.reflectionProbes[capProbe];
            float3 capReflection = parallaxCorrectedReflection(
                reflect(-V, N),
                surface.worldPosition,
                capProbeData.positionRadius.xyz,
                capProbeData.boxExtentsIntensity.xyz,
                capProbeData.positionRadius.w);
            return float4(gProbeMaps[NonUniformResourceIndex(capCube)].SampleLevel(capReflection, 0.0).rgb, 1.0);
        }
    case 16:
        {
            if (gCamera.irradianceVolumeCountPad.x == 0)
            {
                return float4(0.15, 0.0, 0.15, 1.0);
            }

            float3 volumeIrradiance;
            if (!sampleIrradianceVolumes(surface.worldPosition, N, volumeIrradiance))
            {
                return float4(1.0, 0.0, 1.0, 1.0);
            }

            if (dot(volumeIrradiance, volumeIrradiance) < 1e-8)
            {
                return float4(1.0, 1.0, 0.0, 1.0);
            }

            return float4(volumeIrradiance, 1.0);
        }
    default:
        break;
    }

    return float4(0.0, 0.0, 0.0, 0.0);
}

ShadingResult evaluateLighting(Surface surface, ClusterCell cluster)
{
    float3 N = surface.normal;
    float3 V = surface.viewDirection;
    float3 geometricNormal = surface.geometricNormal;
    float3 albedo = surface.albedo;
    float metallic = surface.metallic;
    float roughness = surface.roughness;
    float occlusion = surface.occlusion;

    uint clusterIndex = cluster.index;
    uint clusterCount = cluster.count;
    uint lightCountInCluster = cluster.lightCount;
    float dist = cluster.viewDistance;

    ShadingResult result;
    result.indirectDiffuse = float3(0.0, 0.0, 0.0);
    result.isDebugView = false;

    float3 outIndirectDiffuse = float3(0.0, 0.0, 0.0);

    float3 color = float3(0.0, 0.0, 0.0);

    uint shadowCasterIndex = uint(gCamera.shadowParams.x) - 1;

    float debugMinShadow = 1.0;

    for (uint di = 0; di < gCamera.directionalLightCountPad.x; ++di)
    {
        DirectionalLightGpu light = gCamera.directionalLights[di];
        float3 L = normalize(-light.direction.xyz);
        float3 radiance = light.colorIntensity.rgb * light.colorIntensity.a;

        float shadow = 1.0;
        if (tracedShadowsEnabled())
        {
            shadow = traceShadowRay(surface.worldPosition, geometricNormal, L, 1.0e5);
        }
        else if (gCamera.shadowParams.x >= 0.5 && di == shadowCasterIndex)
        {
            shadow = sampleDirectionalShadow(surface.worldPosition, geometricNormal, L, dist, surface.pixel);
        }

        if (dot(geometricNormal, L) > 0.0)
        {
            debugMinShadow = min(debugMinShadow, shadow);
        }

        color += shadeLight(N, V, L, radiance, albedo, metallic, roughness) * shadow;
    }

    if (clusterIndex < clusterCount)
    {
        uint indexBase = clusterIndex * gClusterParams.clusterDims.w;

        for (uint li = 0; li < lightCountInCluster; ++li)
        {
            GpuLight light = gLights[gLightIndexList[indexBase + li]];

            float3 toLight = light.worldPosRange.xyz - surface.worldPosition;
            float lightDist = length(toLight);
            float3 L = toLight / max(lightDist, 0.0001);

            float atten = attenuateRange(lightDist, light.worldPosRange.w);
            bool isSpot = light.viewPosType.w > 0.5;
            if (isSpot)
            {
                float cosAngle = dot(-L, normalize(light.directionPad.xyz));
                atten *= smoothstep(light.coneAngles.x, light.coneAngles.y, cosAngle);
            }

            float shadow;
            if (tracedShadowsEnabled())
            {
                shadow = traceShadowRay(surface.worldPosition, geometricNormal, L, lightDist);
            }
            else
            {
                shadow = isSpot
                    ? sampleSpotShadow(light, surface.worldPosition, geometricNormal, L, surface.pixel)
                    : samplePointShadow(light, surface.worldPosition, geometricNormal, L, surface.pixel);
            }

            if (dot(geometricNormal, L) > 0.0)
            {
                debugMinShadow = min(debugMinShadow, shadow);
            }

            float3 radiance = light.colorIntensity.rgb * light.colorIntensity.a * atten;

            color += shadeLight(N, V, L, radiance, albedo, metallic, roughness) * shadow;
        }
    }

    if (gCamera.renderModePad.x == 19)
    {
        result.color = float3(debugMinShadow, debugMinShadow, debugMinShadow);
        result.isDebugView = true;
        return result;
    }

    float3 uniformRadiance = gCamera.ambientColorIntensity.rgb * gCamera.ambientColorIntensity.a;
    float NdotV = saturate(dot(N, V));
    bool hasEnvironment = gCamera.environmentParams.x >= 0.5;

    float3 reflection = reflect(-V, N);

    float prefilteredLod = roughness * max(gCamera.environmentParams.y - 1.0, 0.0);

    int bestProbe = -1;
    float bestProbeWeight = 0.0;
    for (uint pi = 0; pi < gCamera.reflectionProbeCountPad.x; ++pi)
    {
        float weight = reflectionProbeWeight(surface.worldPosition, gCamera.reflectionProbes[pi]);
        if (weight > bestProbeWeight)
        {
            bestProbeWeight = weight;
            bestProbe = int(pi);
        }
    }

    float3 irradiance = uniformRadiance;
    float3 prefiltered = uniformRadiance;

    float3 volumeIrradiance;
    bool hasVolumeIrradiance = sampleIrradianceVolumes(surface.worldPosition, N, volumeIrradiance);

    if (hasEnvironment)
    {
        float3 globalIrradiance = gIrradianceMap.SampleLevel(N, 0.0).rgb;
        uint bestProbeCube = bestProbe >= 0 ? uint(gCamera.reflectionProbes[bestProbe].params.x) : 0u;
        if (bestProbe >= 0 && (gCamera.reflectionProbeCountPad.z & (1u << bestProbeCube)) != 0u)
        {
            float3 probeIrradiance =
                gProbeIrradiance[NonUniformResourceIndex(bestProbeCube)].SampleLevel(N, 0.0).rgb *
                gCamera.reflectionProbes[bestProbe].boxExtentsIntensity.w;
            globalIrradiance = lerp(globalIrradiance, probeIrradiance, bestProbeWeight);
        }

        irradiance += hasVolumeIrradiance ? volumeIrradiance : globalIrradiance;

        float3 globalSpecular = gPrefilteredEnv.SampleLevel(reflection, prefilteredLod).rgb;

        if (bestProbe >= 0)
        {
            ReflectionProbeGpu probe = gCamera.reflectionProbes[bestProbe];
            float3 corrected = parallaxCorrectedReflection(
                reflection,
                surface.worldPosition,
                probe.positionRadius.xyz,
                probe.boxExtentsIntensity.xyz,
                probe.positionRadius.w);

            uint probeCube = uint(probe.params.x);
            bool probeBaked = (gCamera.reflectionProbeCountPad.z & (1u << probeCube)) != 0u;
            float3 probeSpecular;
            if (probeBaked)
            {
                float probeLod = roughness * max(float(gCamera.reflectionProbeCountPad.y) - 1.0, 0.0);
                probeSpecular = gProbeMaps[NonUniformResourceIndex(probeCube)].SampleLevel(corrected, probeLod).rgb;
            }
            else
            {
                probeSpecular = gPrefilteredEnv.SampleLevel(corrected, prefilteredLod).rgb;
            }
            probeSpecular *= probe.boxExtentsIntensity.w;
            globalSpecular = lerp(globalSpecular, probeSpecular, bestProbeWeight);
        }

        prefiltered += globalSpecular;
    }
    else if (hasVolumeIrradiance)
    {
        irradiance = volumeIrradiance;
    }

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 specularFresnel = fresnelSchlickRoughness(NdotV, F0, roughness);

    float3 diffuseWeight = (float3(1.0, 1.0, 1.0) - specularFresnel) * (1.0 - metallic);

    float2 envBrdf = environmentBRDF(roughness, NdotV);
    float3 ambientDiffuse = irradiance * albedo * diffuseWeight;
    float3 ambientSpecular = prefiltered * (specularFresnel * envBrdf.x + envBrdf.y);

    color += (ambientDiffuse + ambientSpecular) * occlusion;

    outIndirectDiffuse = ambientDiffuse * occlusion;

    result.color = color;
    result.indirectDiffuse = outIndirectDiffuse;
    return result;
}

ShadingResult shadeSurfaceLighting(Surface surface)
{
    ClusterCell cluster = clusterCellAt(surface.viewSpaceZ, surface.pixel);

    float4 debug = evaluateDebugView(surface, cluster);
    if (debug.a > 0.0)
    {
        ShadingResult result;
        result.color = debug.rgb;
        result.indirectDiffuse = float3(0.0, 0.0, 0.0);
        result.isDebugView = true;
        return result;
    }

    return evaluateLighting(surface, cluster);
}
