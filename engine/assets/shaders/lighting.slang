// Set 0 belongs to the engine and importing shaders own set 1
import gi_sample;

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

    // viewProjection plus the TAA jitter for world raster only
    float4x4 jitteredViewProjection;
    // Unjittered viewProjection of the last presented frame
    float4x4 previousViewProjection;

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

    // x 1 when gScene holds a TLAS and traced shadows are on
    float4 tracedShadowParams;

    /// xy blue noise texel offset walking R2 per frame z frame index
    float4 noiseParams;
};

[[vk::binding(0,0)]]
ConstantBuffer<CameraUBO> gCamera;

struct ClusterParams
{
    float4x4 inverseProjection;
    uint4 clusterDims;         // xyz dims w max lights per cluster
    float4 screenSizeNearFar;  // xy screen size z camera near w camera far
    float4 sliceDepth;         // x log range start y log range end z slices per log unit
    uint4 lightCounts;         // x light count
};

struct GpuLight
{
    float4 worldPosRange;
    float4 viewPosType;     // w type 0 point 1 spot
    float4 viewDirection;   // xyz view space spot axis
    float4 colorIntensity;
    float4 directionPad;    // w shadow layer or -1
    float4 coneAngles;      // x cos outer y cos inner z shadow near w shadow far

    // x fraction of the shadow layer rendered y point light face scale inside the guard band z shadow strength
    float4 shadowAtlas;

    // Spotlight only
    float4x4 shadowViewProjection;

    /// x kLightShape y radius or rect half width z capsule half length or rect half height w kShadowMode
    /// The shape faces or runs along directionPad.xyz
    float4 shape;

    /// xyz rect width axis
    float4 shapeTangent;
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

/// gPunctualShadowMaps without comparison for the PCSS blocker search
[[vk::binding(16,0)]]
Sampler2DArray gPunctualShadowDepths;

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

/// renderer::blue_noise texels with four independent channels a byte each
[[vk::binding(15,0)]]
StructuredBuffer<uint> gBlueNoise;

/// Mirrors renderer::blue_noise::k_size
static const uint kBlueNoiseSize = 64u;

/// Four values in (0 1) decorrelated per sample index
float4 blueNoise(float2 pixel, uint sampleIndex)
{
    const uint2 offset = uint2(gCamera.noiseParams.xy) + sampleIndex * uint2(23u, 41u);
    const uint2 texel = (uint2(pixel) + offset) & (kBlueNoiseSize - 1u);
    const uint packed = gBlueNoise[texel.y * kBlueNoiseSize + texel.x];
    return (float4(float(packed & 0xFFu), float((packed >> 8) & 0xFFu), float((packed >> 16) & 0xFFu), float(packed >> 24)) + 0.5) / 256.0;
}

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

/// Diffuse is over albedo so the denoiser filters texture free light
struct LightTerms
{
    float3 diffuse;
    float3 specular;
};

LightTerms shadeLightTerms(float3 N, float3 V, float3 L, float3 radiance, float3 albedo, float metallic, float roughness,
                           float reflectivity)
{
    LightTerms terms;
    terms.diffuse = float3(0.0, 0.0, 0.0);
    terms.specular = float3(0.0, 0.0, 0.0);

    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0)
    {
        return terms;
    }

    float3 H = normalize(V + L);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    float3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
    float3 specular = numerator / denominator;

    float3 kD = lerp(float3(1.0, 1.0, 1.0), (float3(1.0, 1.0, 1.0) - F) * (1.0 - metallic), reflectivity);

    terms.diffuse = kD / kPi * radiance * NdotL;
    terms.specular = specular * reflectivity * radiance * NdotL;
    return terms;
}

float3 shadeLight(float3 N, float3 V, float3 L, float3 radiance, float3 albedo, float metallic, float roughness, float reflectivity,
                  out float3 specular)
{
    const LightTerms terms = shadeLightTerms(N, V, L, radiance, albedo, metallic, roughness, reflectivity);
    specular = terms.specular;
    return terms.diffuse * albedo + terms.specular;
}

float3 shadeLight(float3 N, float3 V, float3 L, float3 radiance, float3 albedo, float metallic, float roughness, float reflectivity)
{
    float3 specular;
    return shadeLight(N, V, L, radiance, albedo, metallic, roughness, reflectivity, specular);
}

float attenuateRange(float dist, float range)
{
    float rangeFalloff = clamp(1.0 - pow(dist / max(range, 0.0001), 4.0), 0.0, 1.0);
    rangeFalloff = rangeFalloff * rangeFalloff;
    return rangeFalloff / max(dist * dist, 0.0001);
}

// Mirror toast::LightShape and toast::LightShadowMode
static const uint kLightShapePoint = 0u;
static const uint kLightShapeSphere = 1u;
static const uint kLightShapeCapsule = 2u;
static const uint kLightShapeDisc = 3u;
static const uint kLightShapeRect = 4u;
static const uint kShadowModeMap = 0u;
static const uint kShadowModeMarched = 1u;
static const uint kShadowModeSampled = 2u;

uint lightShadowMode(GpuLight light)
{
    return uint(light.shape.w + 0.5);
}

void perpendicularBasis(float3 axis, out float3 tangent, out float3 bitangent)
{
    const float3 reference = abs(axis.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    tangent = normalize(cross(axis, reference));
    bitangent = cross(axis, tangent);
}

float2 concentricDisc(float2 u)
{
    const float radius = sqrt(u.x);
    const float angle = 6.28318531 * u.y;
    return radius * float2(cos(angle), sin(angle));
}

/// A sphere samples its silhouette disc facing the receiver
float3 sampleLightShape(GpuLight light, float3 receiver, float3 u)
{
    const float3 center = light.worldPosRange.xyz;
    const uint shape = uint(light.shape.x + 0.5);
    const float3 axis = light.directionPad.xyz;
    float3 tangent;
    float3 bitangent;

    if (shape == kLightShapeSphere || shape == kLightShapeCapsule)
    {
        float3 core = center;
        if (shape == kLightShapeCapsule && dot(axis, axis) > 0.5)
        {
            core += axis * ((2.0 * u.z - 1.0) * light.shape.z);
        }
        const float3 toReceiver = receiver - core;
        if (dot(toReceiver, toReceiver) < 1e-8)
        {
            return core;
        }
        perpendicularBasis(normalize(toReceiver), tangent, bitangent);
        const float2 disc = concentricDisc(u.xy) * light.shape.y;
        return core + tangent * disc.x + bitangent * disc.y;
    }
    if (shape == kLightShapeDisc && dot(axis, axis) > 0.5)
    {
        perpendicularBasis(axis, tangent, bitangent);
        const float2 disc = concentricDisc(u.xy) * light.shape.y;
        return center + tangent * disc.x + bitangent * disc.y;
    }
    if (shape == kLightShapeRect && dot(axis, axis) > 0.5)
    {
        tangent = light.shapeTangent.xyz;
        bitangent = cross(axis, tangent);
        return center + tangent * ((2.0 * u.x - 1.0) * light.shape.y) + bitangent * ((2.0 * u.y - 1.0) * light.shape.z);
    }
    return center;
}

/// Spot cone from the light centre and flat emitters one sided
float3 lightSampleRadiance(GpuLight light, float3 position, float3 samplePoint, out float3 L, out float distance)
{
    const float3 toLight = samplePoint - position;
    distance = length(toLight);
    L = toLight / max(distance, 1e-4);

    float atten = attenuateRange(distance, light.worldPosRange.w);
    if (light.viewPosType.w > 0.5)
    {
        const float3 fromCenter = position - light.worldPosRange.xyz;
        const float cosAngle = dot(normalize(fromCenter), normalize(light.directionPad.xyz));
        atten *= smoothstep(light.coneAngles.x, light.coneAngles.y, cosAngle);
    }
    const uint shape = uint(light.shape.x + 0.5);
    if ((shape == kLightShapeDisc || shape == kLightShapeRect) && dot(light.directionPad.xyz, -L) <= 0.0)
    {
        atten = 0.0;
    }
    return light.colorIntensity.rgb * light.colorIntensity.a * atten;
}

/// Nearest capsule core point or the centre
float3 analyticLightPoint(GpuLight light, float3 position)
{
    const float3 center = light.worldPosRange.xyz;
    if (uint(light.shape.x + 0.5) != kLightShapeCapsule || dot(light.directionPad.xyz, light.directionPad.xyz) < 0.5)
    {
        return center;
    }
    const float3 axis = light.directionPad.xyz * light.shape.z;
    const float t = saturate(dot(position - (center - axis), axis) / max(2.0 * dot(axis, axis), 1e-8));
    return center - axis + 2.0 * axis * t;
}

/// Karis 2013 representative points
/// Disc and rect use the sphere of equal area
float3 shadeAnalyticLight(GpuLight light, float3 position, float3 N, float3 V, float3 L, float3 radiance, float3 albedo, float metallic,
                          float roughness, float reflectivity, out float3 specularPart)
{
    specularPart = float3(0.0, 0.0, 0.0);
    const uint shape = uint(light.shape.x + 0.5);
    if (shape == kLightShapePoint || light.shape.y + light.shape.z <= 0.0)
    {
        return shadeLight(N, V, L, radiance, albedo, metallic, roughness, reflectivity, specularPart);
    }

    const float3 axis = light.directionPad.xyz;
    const bool oriented = dot(axis, axis) > 0.5;
    const float3 toCenter = light.worldPosRange.xyz - position;
    if ((shape == kLightShapeDisc || shape == kLightShapeRect) && oriented && dot(axis, toCenter) >= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    const float alpha = max(roughness * roughness, 1e-4);
    const float3 R = reflect(-V, N);
    float3 core = toCenter;
    float radius = light.shape.y;
    float lineNormalization = 1.0;
    if (shape == kLightShapeCapsule && oriented)
    {
        const float3 start = toCenter - axis * light.shape.z;
        const float3 segment = axis * (2.0 * light.shape.z);
        const float RdotSegment = dot(R, segment);
        const float t = saturate((dot(R, start) * RdotSegment - dot(start, segment)) /
                                 max(dot(segment, segment) - RdotSegment * RdotSegment, 1e-8));
        core = start + segment * t;
        lineNormalization = alpha / saturate(alpha + light.shape.z / (1.5 * max(length(toCenter), 1e-3)));
    }
    else if (shape == kLightShapeRect)
    {
        radius = sqrt(4.0 * light.shape.y * light.shape.z / kPi);
    }

    const float3 centerToRay = dot(core, R) * R - core;
    const float3 closest = core + centerToRay * saturate(radius / max(length(centerToRay), 1e-6));
    const float coreDistance = max(length(core), 1e-3);
    const float widened = saturate(alpha + radius / (2.0 * coreDistance));

    // A reflection ray missing the emitter by over a lobe width fades back to the centre
    const float miss = max(length(centerToRay) - radius, 0.0) / coreDistance;
    const float hit = 1.0 - saturate(miss / max(alpha, 1e-3));
    const float3 direction = normalize(lerp(normalize(core), normalize(closest), hit));
    const float normalization = lerp(1.0, (alpha / widened) * (alpha / widened) * lineNormalization, hit);

    const LightTerms diffuse = shadeLightTerms(N, V, L, radiance, albedo, metallic, roughness, reflectivity);
    const LightTerms specular = shadeLightTerms(N, V, direction, radiance, albedo, metallic, roughness, reflectivity);
    specularPart = specular.specular * normalization;
    return diffuse.diffuse * albedo + specularPart;
}

float3 shadeAnalyticLight(GpuLight light, float3 position, float3 N, float3 V, float3 L, float3 radiance, float3 albedo, float metallic,
                          float roughness, float reflectivity)
{
    float3 specularPart;
    return shadeAnalyticLight(light, position, N, V, L, radiance, albedo, metallic, roughness, reflectivity, specularPart);
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

/// The first kShadowProbeTaps form a cross
static const float2 kPoissonDisk[12] = {
    float2(-0.840, -0.074), float2( 0.962, -0.195), float2(-0.203,  0.621), float2( 0.185, -0.893),
    float2(-0.326, -0.406), float2(-0.696,  0.457), float2( 0.473, -0.480), float2( 0.519,  0.767),
    float2( 0.507,  0.064), float2( 0.896,  0.412), float2(-0.322, -0.933), float2(-0.792, -0.598),
};

static const uint kShadowProbeTaps = 4u;

/// Walked per frame so TAA averages the rotation
float interleavedGradientNoise(float2 pixel)
{
    const float2 shifted = pixel + 5.588238 * float(uint(gCamera.noiseParams.z) & 63u);
    return frac(52.9829189 * frac(dot(shifted, float2(0.06711056, 0.00583715))));
}

static const float kPunctualBiasMeters = 0.01;
static const float kPunctualBiasTexels = 0.25;

/// Tangent of the steepest receiver slope so about 76 degrees
static const float kMaxReceiverSlope = 4.0;

/// Short enough that perspective depth is linear over it
static const float kDepthProbeMeters = 0.01;

float receiverAdjustLimit(float depthPerMeter, float texelWorldSize)
{
    return depthPerMeter * texelWorldSize * kShadowFilterRadiusTexels * kMaxReceiverSlope;
}

void tangentBasis(float3 normal, out float3 tangent, out float3 bitangent)
{
    const float3 helper = abs(normal.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    tangent = normalize(cross(helper, normal));
    bitangent = cross(normal, tangent);
}

/// dDepth by dUV from three receiver plane points as uv then depth
/// Not screen derivatives which cross deferred and voxel edges
float2 planeDepthGradient(float3 center, float3 alongTangent, float3 alongBitangent)
{
    const float3 a = alongTangent - center;
    const float3 b = alongBitangent - center;
    const float determinant = (a.x * b.y) - (a.y * b.x);
    if (abs(determinant) < 1e-12)
    {
        return float2(0.0, 0.0);
    }
    return float2((a.z * b.y) - (a.y * b.z), (a.x * b.z) - (a.z * b.x)) / determinant;
}

float pcfShadowRadius(
    Sampler2DArrayShadow shadowMaps, float2 uv, float layer, float compareDepth, float texelSize, float2 pixel,
    float2 gradient, float maxGradientAdjust, float radiusTexels)
{
    if (any(uv < float2(0.0, 0.0)) || any(uv > float2(1.0, 1.0)) || compareDepth > 1.0 || compareDepth < 0.0)
    {
        return 1.0;
    }

    float angle = interleavedGradientNoise(pixel) * 6.28318530718;
    float2 rotation = float2(cos(angle), sin(angle));
    float radius = texelSize * radiusTexels;

    // LevelZero since implicit derivatives are undefined under per pixel branches
    // Centre and cross agreeing skips the rest
    float sum = 0.0;
    float probe = shadowMaps.SampleCmpLevelZero(float3(uv, layer), compareDepth);
    [unroll]
    for (uint i = 0u; i < kShadowProbeTaps; ++i)
    {
        const float2 tap = kPoissonDisk[i];
        const float2 offset = float2(tap.x * rotation.x - tap.y * rotation.y, tap.x * rotation.y + tap.y * rotation.x) * radius;
        const float tapDepth = compareDepth + clamp(dot(gradient, offset), -maxGradientAdjust, maxGradientAdjust);
        sum += shadowMaps.SampleCmpLevelZero(float3(uv + offset, layer), tapDepth);
    }
    probe += sum;
    if (probe < 1e-3 || probe > float(kShadowProbeTaps + 1u) - 1e-3)
    {
        return probe / float(kShadowProbeTaps + 1u);
    }

    [unroll]
    for (uint i = kShadowProbeTaps; i < 12u; ++i)
    {
        const float2 tap = kPoissonDisk[i];
        const float2 offset = float2(tap.x * rotation.x - tap.y * rotation.y, tap.x * rotation.y + tap.y * rotation.x) * radius;
        const float tapDepth = compareDepth + clamp(dot(gradient, offset), -maxGradientAdjust, maxGradientAdjust);
        sum += shadowMaps.SampleCmpLevelZero(float3(uv + offset, layer), tapDepth);
    }
    return sum / 12.0;
}

float pcfShadow(
    Sampler2DArrayShadow shadowMaps, float2 uv, float layer, float compareDepth, float texelSize, float2 pixel,
    float2 gradient, float maxGradientAdjust)
{
    return pcfShadowRadius(shadowMaps, uv, layer, compareDepth, texelSize, pixel, gradient, maxGradientAdjust,
                           kShadowFilterRadiusTexels);
}

/// Caps the blocker search cost
static const float kMaxBlockerSearchTexels = 16.0;
static const float kMaxPenumbraTexels = 24.0;

float emitterRadius(GpuLight light)
{
    const uint shape = uint(light.shape.x + 0.5);
    if (shape == kLightShapeCapsule)
    {
        return light.shape.y + 0.5 * light.shape.z;
    }
    if (shape == kLightShapeRect)
    {
        return sqrt(4.0 * light.shape.y * light.shape.z / kPi);
    }
    return shape == kLightShapePoint ? 0.0 : light.shape.y;
}

/// perspectiveRH_ZO depth back to distance along the view axis
float punctualLinearDepth(float depth, GpuLight light)
{
    const float nearPlane = light.coneAngles.z;
    const float farPlane = light.coneAngles.w;
    return farPlane * nearPlane / max(farPlane - depth * (farPlane - nearPlane), 1e-6);
}

/// PCSS filter radius in texels or 0 with no blocker
/// Shadow maps keep standard depth
float penumbraTexels(GpuLight light, float2 uv, float layer, float compareDepth, float receiverDistance, float texelWorldSize,
                     float2 pixel)
{
    const float radius = emitterRadius(light);
    const float texelSize = gCamera.shadowParams.w;
    const float searchTexels = clamp(radius / max(texelWorldSize, 1e-6), kShadowFilterRadiusTexels, kMaxBlockerSearchTexels);

    const float angle = interleavedGradientNoise(pixel + 17.0) * 6.28318530718;
    const float2 rotation = float2(cos(angle), sin(angle));
    float blockerSum = 0.0;
    float blockers = 0.0;
    for (int i = 0; i < 12; ++i)
    {
        const float2 tap = kPoissonDisk[i];
        const float2 offset = float2(tap.x * rotation.x - tap.y * rotation.y, tap.x * rotation.y + tap.y * rotation.x) * searchTexels * texelSize;
        const float depth = gPunctualShadowDepths.SampleLevel(float3(uv + offset, layer), 0.0).r;
        if (depth < compareDepth)
        {
            blockerSum += punctualLinearDepth(depth, light);
            blockers += 1.0;
        }
    }
    if (blockers <= 0.0)
    {
        return 0.0;
    }
    const float blocker = blockerSum / blockers;
    const float penumbraWorld = radius * max(receiverDistance - blocker, 0.0) / max(blocker, 1e-3);
    return clamp(penumbraWorld / max(texelWorldSize, 1e-6), kShadowFilterRadiusTexels, kMaxPenumbraTexels);
}

/// Shadow map uv then depth of a point in an orthographic cascade
float3 cascadeUvDepth(uint cascade, float3 position)
{
    const float3 projected = mul(gCamera.cascadeViewProjection[cascade], float4(position, 1.0)).xyz;
    return float3(projected.xy * 0.5 + 0.5, projected.z);
}

float sampleCascade(uint cascade, float3 worldPosition, float3 N, float3 L, float2 pixel)
{
    float NdotL = saturate(dot(N, L));
    const float texelWorldSize = gCamera.cascadeTexelWorldSize[cascade];
    float3 offsetPosition = worldPosition + N * shadowNormalOffset(texelWorldSize, NdotL);

    const float3 center = cascadeUvDepth(cascade, offsetPosition);
    float3 tangent;
    float3 bitangent;
    tangentBasis(N, tangent, bitangent);
    const float2 gradient = planeDepthGradient(center, cascadeUvDepth(cascade, offsetPosition + tangent * texelWorldSize),
                                               cascadeUvDepth(cascade, offsetPosition + bitangent * texelWorldSize));

    const float texelUv = gCamera.shadowParams.z;
    const float depthPerMeter = abs(center.z - cascadeUvDepth(cascade, offsetPosition + L * kDepthProbeMeters).z) / kDepthProbeMeters;
    const float adjustLimit = receiverAdjustLimit(depthPerMeter, texelWorldSize);
    const float compareDepth = center.z - gCamera.cascadeDepthBias[cascade];
    return pcfShadow(gShadowMap, center.xy, float(cascade), compareDepth, texelUv, pixel, gradient, adjustLimit);
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

/// Share of a cascade map at its border that blends into the next cascade
static const float kCascadeBlendBand = 0.1;
/// Share of the shadow distance over which the last cascade fades out
static const float kShadowDistanceFade = 0.1;

/// Uv distance inside the cascade border or 0 outside its map or depth range
/// Past the far plane a receiver would compare as lit
float cascadeEdge(uint cascade, float3 worldPosition)
{
    const float3 uvDepth = cascadeUvDepth(cascade, worldPosition);
    if (uvDepth.z <= 0.0 || uvDepth.z >= 1.0)
    {
        return 0.0;
    }
    // The filter and normal offset reach a few texels past the point
    const float margin = gCamera.shadowParams.z * (kShadowFilterRadiusTexels + 2.0);
    return min(min(uvDepth.x, uvDepth.y), min(1.0 - uvDepth.x, 1.0 - uvDepth.y)) - margin;
}

/// Finest cascade whose map and depth range hold the point
float sampleDirectionalShadow(float3 worldPosition, float3 N, float3 L, float viewDepth, float2 pixel)
{
    if (gCamera.shadowParams.x < 0.5)
    {
        return 1.0;
    }

    const uint cascadeCount = uint(gCamera.shadowParams.y);
    const float shadowDistance = gCamera.cascadeSplits[cascadeCount - 1u];
    const float distanceFade = saturate((shadowDistance - viewDepth) / (shadowDistance * kShadowDistanceFade));
    if (distanceFade <= 0.0)
    {
        return 1.0;
    }

    for (uint cascade = 0u; cascade < cascadeCount; ++cascade)
    {
        const float edge = cascadeEdge(cascade, worldPosition);
        if (edge <= 0.0)
        {
            continue;
        }

        float shadow = sampleCascade(cascade, worldPosition, N, L, pixel);
        if (edge < kCascadeBlendBand && cascade + 1u < cascadeCount && cascadeEdge(cascade + 1u, worldPosition) > 0.0)
        {
            const float coarser = sampleCascade(cascade + 1u, worldPosition, N, L, pixel);
            shadow = lerp(coarser, shadow, edge / kCascadeBlendBand);
        }
        return lerp(1.0, shadow, distanceFade);
    }
    return 1.0;
}

/// Shadow layer uv then depth of a point in a spot light frustum
float3 spotUvDepth(GpuLight light, float3 position)
{
    const float4 clip = mul(light.shadowViewProjection, float4(position, 1.0));
    const float3 projected = clip.xyz / max(clip.w, 1e-6);
    return float3((projected.xy * 0.5 + 0.5) * light.shadowAtlas.x, projected.z);
}

/// Bias is a world distance toward the light since perspective depth is not linear
float sampleSpotShadow(GpuLight light, float3 worldPosition, float3 N, float3 L, float2 pixel)
{
    float layer = light.directionPad.w;
    if (layer < 0.0)
    {
        return 1.0;
    }

    // A perspective row scales by the reciprocal tangent of the half field of view
    const float tanHalfFov = 1.0 / length(light.shadowViewProjection[0].xyz);
    const float renderedTexels = max(light.shadowAtlas.x, 1e-4) / gCamera.shadowParams.w;
    const float texelWorldSize = 2.0 * distance(worldPosition, light.worldPosRange.xyz) * tanHalfFov / renderedTexels;

    float NdotL = saturate(dot(N, L));
    float3 offsetPosition = worldPosition + N * shadowNormalOffset(texelWorldSize, NdotL);
    if (mul(light.shadowViewProjection, float4(offsetPosition, 1.0)).w <= 0.0)
    {
        return 1.0;
    }

    const float3 center = spotUvDepth(light, offsetPosition);
    const float biasWorld = kPunctualBiasMeters + kPunctualBiasTexels * texelWorldSize;
    const float compareDepth = spotUvDepth(light, offsetPosition + L * biasWorld).z;

    float3 tangent;
    float3 bitangent;
    tangentBasis(N, tangent, bitangent);
    const float2 gradient = planeDepthGradient(center, spotUvDepth(light, offsetPosition + tangent * texelWorldSize),
                                               spotUvDepth(light, offsetPosition + bitangent * texelWorldSize));

    const float depthPerMeter = abs(center.z - spotUvDepth(light, offsetPosition + L * kDepthProbeMeters).z) / kDepthProbeMeters;
    const float adjustLimit = receiverAdjustLimit(depthPerMeter, texelWorldSize);
    float radiusTexels = kShadowFilterRadiusTexels;
    if (emitterRadius(light) > 0.0)
    {
        const float receiver = mul(light.shadowViewProjection, float4(offsetPosition, 1.0)).w;
        radiusTexels = penumbraTexels(light, center.xy, layer, compareDepth, receiver, texelWorldSize, pixel);
        if (radiusTexels <= 0.0)
        {
            return 1.0;
        }
    }
    const float shadow = pcfShadowRadius(gPunctualShadowMaps, center.xy, layer, compareDepth, gCamera.shadowParams.w, pixel, gradient,
                                         adjustLimit, radiusTexels);
    return lerp(1.0, shadow, light.shadowAtlas.z);
}

/// Cube face of a direction from the light in cubeFaceBasis() order +X -X +Y -Y +Z -Z
uint pointFace(float3 direction)
{
    const float3 absDirection = abs(direction);
    if (absDirection.x >= absDirection.y && absDirection.x >= absDirection.z)
    {
        return direction.x > 0.0 ? 0u : 1u;
    }
    if (absDirection.y >= absDirection.z)
    {
        return direction.y > 0.0 ? 2u : 3u;
    }
    return direction.z > 0.0 ? 4u : 5u;
}

struct PointFaceFrame
{
    float3 u;
    float3 v;
    float3 major;
    /// shadowAtlas.y and shadowAtlas.x
    float ndcScale;
    float atlas;
    /// perspectiveRH_ZO depth is depthNear - depthFar / majorAxis
    float depthNear;
    float depthFar;
};

/// Must agree with cubeFaceBasis() or shadows tear at face seams
PointFaceFrame pointFaceFrame(GpuLight light, uint face)
{
    PointFaceFrame frame;
    const uint axis = face >> 1;
    frame.major = float3(0.0, 0.0, 0.0);
    frame.major[axis] = 1.0;
    const bool zFace = axis == 2u;
    frame.v = zFace ? float3(0.0, 1.0, 0.0) : float3(0.0, 0.0, 1.0);
    const float uSign = (face == 0u || face == 3u || face == 4u) ? -1.0 : 1.0;
    frame.u = axis == 0u ? float3(0.0, uSign, 0.0) : float3(uSign, 0.0, 0.0);
    frame.ndcScale = light.shadowAtlas.y;
    frame.atlas = light.shadowAtlas.x;
    const float nearPlane = light.coneAngles.z;
    const float farPlane = light.coneAngles.w;
    const float range = max(farPlane - nearPlane, 0.0001);
    frame.depthNear = farPlane / range;
    frame.depthFar = farPlane * nearPlane / range;
    return frame;
}

float3 pointFaceUvDepth(PointFaceFrame frame, float3 direction)
{
    const float majorAxis = max(abs(dot(direction, frame.major)), 1e-4);
    const float2 ndc = float2(dot(direction, frame.u), dot(direction, frame.v)) * (frame.ndcScale / majorAxis);
    return float3((ndc * 0.5 + 0.5) * frame.atlas, frame.depthNear - frame.depthFar / majorAxis);
}

float pointFaceDepth(PointFaceFrame frame, float3 direction)
{
    return frame.depthNear - frame.depthFar / max(abs(dot(direction, frame.major)), 1e-4);
}

float samplePointShadow(GpuLight light, float3 worldPosition, float3 N, float3 L, float2 pixel)
{
    float baseLayer = light.directionPad.w;
    if (baseLayer < 0.0)
    {
        return 1.0;
    }

    // Guard texels widen each face so its half field of view has a tangent of 1 over shadowAtlas.y
    const float renderedTexels = max(light.shadowAtlas.x, 1e-4) / gCamera.shadowParams.w;
    const float texelWorldSize =
        2.0 * distance(worldPosition, light.worldPosRange.xyz) / (max(light.shadowAtlas.y, 1e-4) * renderedTexels);

    float NdotL = saturate(dot(N, L));
    float3 offsetPosition = worldPosition + N * shadowNormalOffset(texelWorldSize, NdotL);

    const float3 direction = offsetPosition - light.worldPosRange.xyz;
    if (dot(direction, direction) < 1e-8)
    {
        return 1.0;
    }

    const uint face = pointFace(direction);
    const PointFaceFrame frame = pointFaceFrame(light, face);
    const float3 center = pointFaceUvDepth(frame, direction);
    const float biasWorld = kPunctualBiasMeters + kPunctualBiasTexels * texelWorldSize;
    const float compareDepth = pointFaceDepth(frame, direction + L * biasWorld);

    float3 tangent;
    float3 bitangent;
    tangentBasis(N, tangent, bitangent);
    const float2 gradient = planeDepthGradient(center, pointFaceUvDepth(frame, direction + tangent * texelWorldSize),
                                               pointFaceUvDepth(frame, direction + bitangent * texelWorldSize));

    // d depth / d majorAxis is depthFar / majorAxis squared
    const float majorAxis = max(abs(dot(direction, frame.major)), 1e-4);
    const float depthPerMeter = frame.depthFar * abs(dot(L, frame.major)) / (majorAxis * majorAxis);
    const float adjustLimit = receiverAdjustLimit(depthPerMeter, texelWorldSize);
    float radiusTexels = kShadowFilterRadiusTexels;
    if (emitterRadius(light) > 0.0)
    {
        radiusTexels = penumbraTexels(light, center.xy, baseLayer + float(face), compareDepth, abs(direction[face >> 1]), texelWorldSize, pixel);
        if (radiusTexels <= 0.0)
        {
            return 1.0;
        }
    }
    const float shadow = pcfShadowRadius(gPunctualShadowMaps, center.xy, baseLayer + float(face), compareDepth, gCamera.shadowParams.w,
                                         pixel, gradient, adjustLimit, radiusTexels);
    return lerp(1.0, shadow, light.shadowAtlas.z);
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

/// One compare tap since scattering blurs any filtered edge
float volumeSunShadow(float3 worldPosition)
{
    if (gCamera.shadowParams.x < 0.5)
    {
        return 1.0;
    }
    const uint cascadeCount = uint(gCamera.shadowParams.y);
    for (uint cascade = 0u; cascade < cascadeCount; ++cascade)
    {
        if (cascadeEdge(cascade, worldPosition) <= 0.0)
        {
            continue;
        }
        const float3 center = cascadeUvDepth(cascade, worldPosition);
        return gShadowMap.SampleCmpLevelZero(float3(center.xy, float(cascade)), center.z - gCamera.cascadeDepthBias[cascade]);
    }
    return 1.0;
}

/// One compare tap in the spot or cube face map and 1 for a light without one
float volumeLocalShadow(GpuLight light, float3 worldPosition)
{
    const float layer = light.directionPad.w;
    if (layer < 0.0)
    {
        return 1.0;
    }
    const float3 direction = worldPosition - light.worldPosRange.xyz;
    if (dot(direction, direction) < 1e-8)
    {
        return 1.0;
    }
    const float3 toLight = -normalize(direction);
    if (light.viewPosType.w > 0.5)
    {
        if (mul(light.shadowViewProjection, float4(worldPosition, 1.0)).w <= 0.0)
        {
            return 1.0;
        }
        const float3 center = spotUvDepth(light, worldPosition);
        const float compareDepth = spotUvDepth(light, worldPosition + toLight * kPunctualBiasMeters).z;
        return gPunctualShadowMaps.SampleCmpLevelZero(float3(center.xy, layer), compareDepth);
    }
    const uint face = pointFace(direction);
    const PointFaceFrame frame = pointFaceFrame(light, face);
    const float3 center = pointFaceUvDepth(frame, direction);
    const float compareDepth = pointFaceDepth(frame, direction + toLight * kPunctualBiasMeters);
    return gPunctualShadowMaps.SampleCmpLevelZero(float3(center.xy, layer + float(face)), compareDepth);
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

    /// Blends 1 full metallic roughness PBR down to 0 pure Lambert
    float  reflectivity;

    float  occlusion;

    float  viewSpaceZ;

    float2 pixel;

    /// Marched and sampled lights are left to the caller
    /// Every Surface must set it since Slang leaves a plain declaration uninitialized
    bool marchedShadows;
};

struct ShadingResult
{
    float3 color;

    /// Specular share of color
    float3 specular;

    float3 indirectDiffuse;

    /// Environment and probe share of specular which traced specular replaces on voxels
    float3 ambientSpecular;

    /// Meters below which the radiance cascades blur occlusion and 0 where they did not light the point
    float contactReach;

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
    surface.marchedShadows = false;
    surface.viewSpaceZ = viewSpaceZ;
    surface.pixel = pixel;

    float dist = max(-surface.viewSpaceZ, gClusterParams.screenSizeNearFar.z);
    uint dimX = gClusterParams.clusterDims.x;
    uint dimY = gClusterParams.clusterDims.y;
    uint dimZ = gClusterParams.clusterDims.z;

    uint zSlice = uint(clamp(
        log(dist / gClusterParams.sliceDepth.x) * gClusterParams.sliceDepth.z, 0.0, float(dimZ - 1)));

    float2 tileSize = gClusterParams.screenSizeNearFar.xy / float2(dimX, dimY);
    uint2 tile = uint2(clamp(surface.pixel / tileSize, float2(0.0, 0.0), float2(dimX - 1, dimY - 1)));

    uint clusterIndex = tile.x + tile.y * dimX + zSlice * dimX * dimY;
    uint clusterCount = dimX * dimY * dimZ;

    uint lightCountInCluster = 0;
    if (clusterIndex < clusterCount)
    {
        lightCountInCluster = min(gClusterLightGrid[clusterIndex], gClusterParams.clusterDims.w);
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
static const uint kRenderModeVoxelPaletteId = 25;

bool isVoxelDebugMode(uint mode)
{
    return mode >= kRenderModeVoxelSteps && mode <= kRenderModeVoxelPaletteId;
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
        {
            float3 cascadeIrradiance;
            if (sampleRadianceCascades(surface.worldPosition, N, cascadeIrradiance))
            {
                return float4(cascadeIrradiance, 1.0);
            }
            return float4(
                gCamera.ambientColorIntensity.rgb * gCamera.ambientColorIntensity.a +
                    (gCamera.environmentParams.x >= 0.5 ? gIrradianceMap.SampleLevel(N, 0.0).rgb : float3(0.0, 0.0, 0.0)),
                1.0);
        }
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

/// Colour a shadow map cannot hold such as voxel glass
interface IShadowFilter
{
    /// @p distance is negative for a directional light
    float3 filter(float3 L, float distance);
}

struct NoShadowFilter : IShadowFilter
{
    float3 filter(float3 L, float distance)
    {
        return float3(1.0, 1.0, 1.0);
    }
}

int bestReflectionProbe(float3 worldPosition, out float weight)
{
    int best = -1;
    weight = 0.0;
    for (uint pi = 0; pi < gCamera.reflectionProbeCountPad.x; ++pi)
    {
        const float probeWeight = reflectionProbeWeight(worldPosition, gCamera.reflectionProbes[pi]);
        if (probeWeight > weight)
        {
            weight = probeWeight;
            best = int(pi);
        }
    }
    return best;
}

/// Uniform ambient plus the prefiltered environment blended toward the best probe
float3 ambientSpecularRadiance(float3 worldPosition, float3 reflection, float roughness, int bestProbe, float bestProbeWeight)
{
    const float3 uniformRadiance = gCamera.ambientColorIntensity.rgb * gCamera.ambientColorIntensity.a;
    if (gCamera.environmentParams.x < 0.5)
    {
        return uniformRadiance;
    }

    const float prefilteredLod = roughness * max(gCamera.environmentParams.y - 1.0, 0.0);
    float3 globalSpecular = gPrefilteredEnv.SampleLevel(reflection, prefilteredLod).rgb;
    if (bestProbe >= 0)
    {
        ReflectionProbeGpu probe = gCamera.reflectionProbes[bestProbe];
        float3 corrected = parallaxCorrectedReflection(
            reflection,
            worldPosition,
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
    return uniformRadiance + globalSpecular;
}

float3 ambientSpecularRadiance(float3 worldPosition, float3 reflection, float roughness)
{
    float weight;
    const int best = bestReflectionProbe(worldPosition, weight);
    return ambientSpecularRadiance(worldPosition, reflection, roughness, best, weight);
}

ShadingResult evaluateLighting<Filter : IShadowFilter>(Surface surface, ClusterCell cluster, Filter shadowFilter)
{
    float3 N = surface.normal;
    float3 V = surface.viewDirection;
    float3 geometricNormal = surface.geometricNormal;
    float3 albedo = surface.albedo;
    float metallic = surface.metallic;
    float roughness = surface.roughness;
    float reflectivity = saturate(surface.reflectivity);
    float occlusion = surface.occlusion;

    uint clusterIndex = cluster.index;
    uint clusterCount = cluster.count;
    uint lightCountInCluster = cluster.lightCount;
    float dist = cluster.viewDistance;

    ShadingResult result;
    result.indirectDiffuse = float3(0.0, 0.0, 0.0);
    result.specular = float3(0.0, 0.0, 0.0);
    result.ambientSpecular = float3(0.0, 0.0, 0.0);
    result.contactReach = 0.0;
    result.isDebugView = false;

    float3 outIndirectDiffuse = float3(0.0, 0.0, 0.0);
    float3 specular = float3(0.0, 0.0, 0.0);

    float3 color = float3(0.0, 0.0, 0.0);

    uint shadowCasterIndex = uint(gCamera.shadowParams.x) - 1;

    float debugMinShadow = 1.0;
    bool shadowDebugView = gCamera.renderModePad.x == 19;

    for (uint di = 0; di < gCamera.directionalLightCountPad.x; ++di)
    {
        DirectionalLightGpu light = gCamera.directionalLights[di];
        if (surface.marchedShadows && light.direction.w > 0.0)
        {
            continue;
        }
        float3 L = normalize(-light.direction.xyz);
        if (dot(N, L) <= 0.0 && !shadowDebugView)
        {
            continue;
        }

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

        const float3 tint = shadow > 0.0 ? shadowFilter.filter(L, -1.0) : float3(0.0, 0.0, 0.0);
        const float3 weight = shadow * tint;
        float3 lightSpecular;
        color += shadeLight(N, V, L, radiance, albedo, metallic, roughness, reflectivity, lightSpecular) * weight;
        specular += lightSpecular * weight;
    }

    if (clusterIndex < clusterCount)
    {
        uint indexBase = clusterIndex * gClusterParams.clusterDims.w;

        for (uint li = 0; li < lightCountInCluster; ++li)
        {
            const uint lightIndex = gLightIndexList[indexBase + li];
            // Rejected from two float4 before the full load
            // A capsule core lies within shape.z of the centre and every other shape lights from the centre
            const float4 positionRange = gLights[lightIndex].worldPosRange;
            const float4 shapeParams = gLights[lightIndex].shape;
            if (surface.marchedShadows && uint(shapeParams.w + 0.5) != kShadowModeMap)
            {
                continue;
            }
            const float3 toCenter = positionRange.xyz - surface.worldPosition;
            const float reach = positionRange.w + shapeParams.z;
            if (!shadowDebugView && (dot(toCenter, toCenter) >= reach * reach || dot(N, toCenter) + shapeParams.z <= 0.0))
            {
                continue;
            }

            GpuLight light = gLights[lightIndex];

            float3 toLight = analyticLightPoint(light, surface.worldPosition) - surface.worldPosition;
            float lightDist = length(toLight);
            float3 L = toLight / max(lightDist, 0.0001);

            float atten = attenuateRange(lightDist, light.worldPosRange.w);
            bool isSpot = light.viewPosType.w > 0.5;
            if (isSpot)
            {
                float cosAngle = dot(-L, normalize(light.directionPad.xyz));
                atten *= smoothstep(light.coneAngles.x, light.coneAngles.y, cosAngle);
            }

            // Zero contribution so skip before any shadow or ray work
            if ((atten <= 0.0 || dot(N, L) <= 0.0) && !shadowDebugView)
            {
                continue;
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

            const float3 tint = shadow > 0.0 ? shadowFilter.filter(L, lightDist) : float3(0.0, 0.0, 0.0);
            const float3 weight = shadow * tint;
            float3 lightSpecular;
            color += shadeAnalyticLight(light, surface.worldPosition, N, V, L, radiance, albedo, metallic, roughness, reflectivity,
                                        lightSpecular) * weight;
            specular += lightSpecular * weight;
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

    float bestProbeWeight;
    const int bestProbe = bestReflectionProbe(surface.worldPosition, bestProbeWeight);

    float3 irradiance = uniformRadiance;
    const float3 prefiltered = ambientSpecularRadiance(surface.worldPosition, reflect(-V, N), roughness, bestProbe, bestProbeWeight);

    float3 cascadeIrradiance;
    const bool hasCascades = sampleRadianceCascades(surface.worldPosition, N, cascadeIrradiance);

    float3 volumeIrradiance = float3(0.0, 0.0, 0.0);
    bool hasVolumeIrradiance = !hasCascades && sampleIrradianceVolumes(surface.worldPosition, N, volumeIrradiance);

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
    }
    else if (hasVolumeIrradiance)
    {
        irradiance = volumeIrradiance;
    }

    // The cascades already hold the sky and what it bounces off so they replace ambient and baked volumes
    if (hasCascades)
    {
        irradiance = cascadeIrradiance;
        result.contactReach = radianceCascadesContactReach(surface.worldPosition);
    }

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 specularFresnel = fresnelSchlickRoughness(NdotV, F0, roughness);

    float3 diffuseWeight = lerp(float3(1.0, 1.0, 1.0), (float3(1.0, 1.0, 1.0) - specularFresnel) * (1.0 - metallic), reflectivity);

    float2 envBrdf = environmentBRDF(roughness, NdotV);
    float3 ambientDiffuse = irradiance * albedo * diffuseWeight;
    float3 ambientSpecular = prefiltered * (specularFresnel * envBrdf.x + envBrdf.y) * reflectivity;

    color += (ambientDiffuse + ambientSpecular) * occlusion;
    specular += ambientSpecular * occlusion;

    outIndirectDiffuse = ambientDiffuse * occlusion;

    result.ambientSpecular = ambientSpecular * occlusion;
    result.color = color;
    result.specular = specular;
    result.indirectDiffuse = outIndirectDiffuse;
    return result;
}

ShadingResult shadeSurfaceLighting<Filter : IShadowFilter>(Surface surface, Filter shadowFilter)
{
    ClusterCell cluster = clusterCellAt(surface.viewSpaceZ, surface.pixel);

    float4 debug = evaluateDebugView(surface, cluster);
    if (debug.a > 0.0)
    {
        ShadingResult result;
        result.color = debug.rgb;
        result.specular = float3(0.0, 0.0, 0.0);
        result.indirectDiffuse = float3(0.0, 0.0, 0.0);
        result.ambientSpecular = float3(0.0, 0.0, 0.0);
        result.contactReach = 0.0;
        result.isDebugView = true;
        return result;
    }

    return evaluateLighting(surface, cluster, shadowFilter);
}

ShadingResult shadeSurfaceLighting(Surface surface)
{
    NoShadowFilter none;
    return shadeSurfaceLighting(surface, none);
}
