import fullscreen;
import voxel_dda;
import voxel_gbuffer;
import gi_sample;
import lighting;
import scene_trace;
import scene_normal;

// Mirrors VoxelPass::LightingPushConstants
struct LightingPushConstants
{
    // Must be jittered since depth was rasterized with it
    float4x4 inverseViewProjection;
    // xy viewport size
    float4 viewport;
    // x instances with bounds y light candidates z nonzero when fragmentMarched shades w sun reach in meters
    uint4 lighting;
    // x bit 0 last frame's reservoirs are valid bit 1 fragmentSpatial picks and shades sampled lights bit 2 glass shadows
    // bit 3 ray query ambient bit 4 traced specular bits 8 to 15 its max roughness in 255ths and the high 16 bits the
    // glass shadow reach of a sun in meters
    // y M cap z reservoirs reused w ambient ray reach in centimetres and 0 for none
    uint4 reuse;
};

[[vk::push_constant]]
ConstantBuffer<LightingPushConstants> gPush;

// Set 1 is the voxel storage of voxel_dda.slang so shadow rays can march
/// Bounding spheres in gVoxelInstances order with off screen casters last
[[vk::binding(7, 1)]]
StructuredBuffer<float4> gInstanceBounds;

/// Mirrors renderer::VoxelPass::writeAmbientGrid
/// Cell size bits, global offset and count, pad, offset and count per bucket, instance indices
[[vk::binding(8, 1)]]
StructuredBuffer<uint> gAmbientGrid;

static const uint kAmbientGridBuckets = 4096u;
static const uint kAmbientGridHeader = 4u;

uint ambientGridBucket(int3 cell)
{
    const uint3 u = uint3(cell);
    return ((u.x * 73856093u) ^ (u.y * 19349663u) ^ (u.z * 83492791u)) & (kAmbientGridBuckets - 1u);
}

[[vk::binding(0, 2)]]
Sampler2D gAlbedo;

[[vk::binding(1, 2)]]
Sampler2D gFaceNormal;

[[vk::binding(2, 2)]]
Sampler2D gMaterial;

[[vk::binding(3, 2)]]
Sampler2D gDepth;

/// packReservoir of last frame
[[vk::binding(4, 2)]]
Sampler2D<uint4> gPreviousReservoirs;

/// xy uv motion z reactive w 1 when written
[[vk::binding(5, 2)]]
Sampler2D gMotion;

/// This frame's reservoirs before spatial reuse
[[vk::binding(6, 2)]]
Sampler2D<uint4> gCandidateReservoirs;

/// Scene normal of fragmentMain
[[vk::binding(7, 2)]]
Sampler2D gSceneNormal;

/// Marched terms fragmentMarchedComposite adds when the denoiser is off
[[vk::binding(8, 2)]]
Sampler2D gNoisyDiffuse;

[[vk::binding(9, 2)]]
Sampler2D gNoisySpecular;

typedef FullscreenVSOutput VSOutput;

struct FSOutput
{
    float4 color : SV_Target0;
    float4 indirectDiffuse : SV_Target1;
    /// xyz normal w SSR roughness and zero where SSAO and SSR pass through
    float4 normalRoughness : SV_Target2;
    /// Indirect over albedo that fragmentAmbient occludes when ambient rays are on
    float4 noisyDiffuse : SV_Target3;
};

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

bool isLitVoxelAt(int2 pixel, out float3 faceNormal)
{
    const float4 encoded = gFaceNormal.Load(int3(pixel, 0));
    faceNormal = decodeFaceNormal(encoded);
    return encoded.a > 0.5;
}

/// Mirrors voxel::k_voxel_size
static const float kVoxelSize = 0.1;

/// Shadow origin offset off its face in voxels
static const float kShadowOriginOffset = 0.6;

/// Transmittance along the segment and 0 once a voxel blocks it
float3 marchedVisibility(float3 origin, float3 target, float coneSlope, bool glassScene = true)
{
    return traceVisibility(origin, target - origin, coneSlope, glassScene && (gPush.reuse.x & kSceneGlass) != 0u);
}

/// Mesh shadow of a marched light from its map since traced shadows are off with voxels
float meshShadowFactor(GpuLight light, Surface surface)
{
    const float3 toLight = light.worldPosRange.xyz - surface.worldPosition;
    const float distance = length(toLight);
    const float3 L = toLight / max(distance, 1e-4);
    if (light.directionPad.w < 0.0)
    {
        return 1.0;
    }
    return light.viewPosType.w > 0.5 ? sampleSpotShadow(light, surface.worldPosition, surface.geometricNormal, L, surface.pixel)
                                     : samplePointShadow(light, surface.worldPosition, surface.geometricNormal, L, surface.pixel);
}

/// Shadow maps hold no glass so voxel pixels march toward the light through glass volumes
struct VoxelGlassFilter : IShadowFilter
{
    float3 origin;
    float sunReach;
    float sunSlope;

    float3 filter(float3 L, float distance)
    {
        if ((gPush.reuse.x & kSceneGlass) == 0u)
        {
            return float3(1.0, 1.0, 1.0);
        }
        const float3 segment = L * (distance < 0.0 ? sunReach : distance);
        const float slope = distance < 0.0 ? sunSlope : 0.0;
        return traceGlassFilter(origin, segment, slope);
    }

    bool shadesMapLights()
    {
        return true;
    }

    float traceMeshShadow(float3 position, float3 normal, float3 L, float distance)
    {
        return 1.0;
    }
}

/// fragmentMain shades shadow mapped lights without glass and traces nothing
struct VoxelRayFreeFilter : IShadowFilter
{
    bool mapLights;

    float3 filter(float3 L, float distance)
    {
        return float3(1.0, 1.0, 1.0);
    }

    bool shadesMapLights()
    {
        return mapLights;
    }

    float traceMeshShadow(float3 position, float3 normal, float3 L, float distance)
    {
        return 1.0;
    }
}

/// One per pixel since the blue noise turns every frame and the denoiser accumulates the rest
static const uint kAmbientRays = 1u;

/// Teardown style AO from the share of the reach cosine rays travel
float ambientOcclusion(float3 normal, float2 pixel, float3 origin, float reach)
{
    float3 tangent;
    float3 bitangent;
    perpendicularBasis(normal, tangent, bitangent);
    float3 segments[kAmbientRays];
    float travelled[kAmbientRays];
    [unroll]
    for (uint r = 0u; r < kAmbientRays; ++r)
    {
        const float2 disc = concentricDisc(blueNoise(pixel, 120u + r).xy);
        segments[r] = (tangent * disc.x + bitangent * disc.y + normal * sqrt(saturate(1.0 - dot(disc, disc)))) * reach;
        travelled[r] = 1.0;
    }

#if TOAST_RAY_QUERY
    // Exact first hits through the TLAS instead of eight samples per volume
    if ((gPush.reuse.x & kAmbientRayQuery) != 0u && traceRayQueryEnabled())
    {
        float reached = 0.0;
        [unroll]
        for (uint r = 0u; r < kAmbientRays; ++r)
        {
            const SceneHit hit = traceScene(origin, segments[r], 0.0, 1.0, 0.0);
            reached += hit.hit ? hit.t : 1.0;
        }
        return reached / float(kAmbientRays);
    }
#endif

    const float cellSize = asfloat(gAmbientGrid[0]);
    if (!(cellSize > 0.0))
    {
        return 1.0;
    }
    const uint bucket = ambientGridBucket(int3(floor(origin / cellSize)));
    const uint indexBase = kAmbientGridHeader + 2u * kAmbientGridBuckets;
    const uint cellOffset = gAmbientGrid[kAmbientGridHeader + 2u * bucket];
    const uint cellCount = gAmbientGrid[kAmbientGridHeader + 2u * bucket + 1u];
    const uint globalOffset = gAmbientGrid[1];
    const uint total = cellCount + gAmbientGrid[2];
    const float invReach2 = 1.0 / (reach * reach);
    for (uint j = 0u; j < total; ++j)
    {
        const uint i = gAmbientGrid[indexBase + (j < cellCount ? cellOffset + j : globalOffset + j - cellCount)];
        const float4 bounds = gInstanceBounds[i];
        const float3 offset = bounds.xyz - origin;
        const float touch = bounds.w + reach;
        if (dot(offset, offset) > touch * touch)
        {
            continue;
        }

        uint rays = 0u;
        [unroll]
        for (uint r = 0u; r < kAmbientRays; ++r)
        {
            const float along = clamp(dot(offset, segments[r]) * invReach2, 0.0, travelled[r]);
            const float3 closest = segments[r] * along - offset;
            rays |= dot(closest, closest) <= bounds.w * bounds.w ? 1u << r : 0u;
        }
        if (rays == 0u)
        {
            continue;
        }

        const float4x4 worldToVoxel = gVoxelInstances[i].worldToVoxel;
        const uint record = gVoxelInstances[i].recordIndex;
        const float3 voxelOrigin = mul(worldToVoxel, float4(origin, 1.0)).xyz;
        [unroll]
        for (uint r = 0u; r < kAmbientRays; ++r)
        {
            if ((rays & (1u << r)) == 0u)
            {
                continue;
            }
            const float3 voxelSegment = mul((float3x3)worldToVoxel, segments[r]);
            // Five fixed samples then three doubling ones end exactly at the segment end when the first step is a fortieth
            const RayHit hit = marchSuperSparse(record, voxelOrigin, voxelSegment, 0.0, travelled[r], length(voxelSegment) * 0.025);
            if (hit.hit)
            {
                travelled[r] = min(travelled[r], hit.t);
            }
        }
    }

    float sum = 0.0;
    [unroll]
    for (uint r = 0u; r < kAmbientRays; ++r)
    {
        sum += travelled[r];
    }
    return sum / float(kAmbientRays);
}

/// Cone slope from the receiver to the emitter
float shadowConeSlope(GpuLight light, float distance)
{
    return min(0.5 * (light.shape.y + light.shape.z) / max(distance, 1e-3), 0.25);
}

float3 sampleCone(float3 axis, float radius, float2 u)
{
    const float cosTheta = lerp(1.0, cos(radius), u.x);
    const float sinTheta = sqrt(saturate(1.0 - cosTheta * cosTheta));
    const float phi = 6.28318531 * u.y;
    float3 tangent;
    float3 bitangent;
    perpendicularBasis(axis, tangent, bitangent);
    return normalize(axis * cosTheta + (tangent * cos(phi) + bitangent * sin(phi)) * sinTheta);
}

/// Representative point since a glossy highlight from a random point sparkles
float3 analyticSpecular(GpuLight light, Surface surface)
{
    float3 L;
    float distance;
    const float3 radiance = lightSampleRadiance(light, surface.worldPosition, analyticLightPoint(light, surface.worldPosition), L, distance);
    if (all(radiance <= 0.0) || dot(surface.normal, L) <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }
    float3 specular;
    shadeAnalyticLight(light, surface.worldPosition, surface.normal, surface.viewDirection, L, radiance, surface.albedo, surface.metallic,
                       surface.roughness, surface.reflectivity, specular);
    return specular;
}

/// Marched suns as area lights with the cascade kept for mesh shadows
LightTerms shadeMarchedSuns(Surface surface, float3 shadowOrigin)
{
    LightTerms terms;
    terms.diffuse = float3(0.0, 0.0, 0.0);
    terms.specular = float3(0.0, 0.0, 0.0);

    const uint casterIndex = uint(gCamera.shadowParams.x) - 1u;
    const float reach = float(gPush.lighting.w);
    for (uint di = 0u; di < gCamera.directionalLightCountPad.x; ++di)
    {
        const DirectionalLightGpu light = gCamera.directionalLights[di];
        const float radius = light.direction.w;
        if (radius <= 0.0)
        {
            continue;
        }
        const float3 center = normalize(-light.direction.xyz);
        const float3 L = sampleCone(center, radius, blueNoise(surface.pixel, 200u + di).xy);
        if (dot(surface.normal, L) <= 0.0)
        {
            continue;
        }

        float meshShadow = 1.0;
        if (gCamera.shadowParams.x >= 0.5 && di == casterIndex)
        {
            meshShadow = sampleDirectionalShadow(surface.worldPosition, surface.geometricNormal, center, -surface.viewSpaceZ, surface.pixel);
        }
        if (meshShadow <= 0.0)
        {
            continue;
        }

        const float3 visibility = min(meshShadow, marchedVisibility(shadowOrigin, shadowOrigin + L * reach, radius));
        if (all(visibility <= 0.0))
        {
            continue;
        }
        const float3 radiance = light.colorIntensity.rgb * light.colorIntensity.a;
        const LightTerms sample = shadeLightTerms(surface.normal, surface.viewDirection, L, radiance, surface.albedo, surface.metallic,
                                                  surface.roughness, surface.reflectivity);
        const LightTerms centred = shadeLightTerms(surface.normal, surface.viewDirection, center, radiance, surface.albedo, surface.metallic,
                                                   surface.roughness, surface.reflectivity);
        terms.diffuse += sample.diffuse * visibility;
        terms.specular += centred.specular * visibility;
    }
    return terms;
}

struct Reservoir
{
    /// asuint of GpuLight::shapeTangent.w stable across light list rebuilds
    uint lightId;
    /// Unbiased contribution weight and 0 for an empty reservoir
    float W;
    /// sampleLightShape numbers
    float3 u;
    /// Candidates behind it
    uint M;
};

uint4 packReservoir(Reservoir reservoir)
{
    const uint3 u = uint3(saturate(reservoir.u) * 65535.0 + 0.5);
    return uint4(reservoir.lightId, asuint(reservoir.W), u.x | (u.y << 16), u.z | (min(reservoir.M, 65535u) << 16));
}

Reservoir unpackReservoir(uint4 packed)
{
    Reservoir reservoir;
    reservoir.lightId = packed.x;
    reservoir.W = asfloat(packed.y);
    reservoir.u = float3(float(packed.z & 0xFFFFu), float(packed.z >> 16), float(packed.w & 0xFFFFu)) / 65535.0;
    reservoir.M = packed.w >> 16;
    return reservoir;
}

/// Pixels around the reprojected one whose last reservoir joins
static const float kReuseRadius = 8.0;

static const uint kReuseHistory = 1u;
static const uint kReuseSpatial = 2u;
static const uint kSceneGlass = 4u;
static const uint kAmbientRayQuery = 8u;
static const uint kTracedSpecular = 16u;

/// Scene normal roughness of a pixel whose environment specular voxel_specular.slang replaces and SSR skips
static const float kTracedMark = 2.0;

/// gLights index of a sampled light by stable id in one froxel list
uint findSampledLight(uint lightId, uint indexBase, uint lightCount)
{
    for (uint li = 0u; li < lightCount; ++li)
    {
        const uint index = gLightIndexList[indexBase + li];
        if (asuint(gLights[index].shapeTangent.w) == lightId)
        {
            return lightShadowMode(gLights[index]) == kShadowModeSampled ? index : 0xFFFFFFFFu;
        }
    }
    return 0xFFFFFFFFu;
}

struct SampledLight
{
    float3 L;
    float3 radiance;
    /// Unshadowed irradiance luminance
    float target;
    float3 samplePoint;
    float slope;
    float3 u;
    uint lightId;
    /// gLights index this frame
    uint lightIndex;
};

SampledLight evaluateSampledLight(Surface surface, GpuLight light, float3 u)
{
    SampledLight sample;
    sample.target = 0.0;
    sample.L = float3(0.0, 0.0, 1.0);
    sample.radiance = float3(0.0, 0.0, 0.0);
    sample.u = u;
    sample.lightId = asuint(light.shapeTangent.w);
    sample.lightIndex = 0u;
    sample.samplePoint = sampleLightShape(light, surface.worldPosition, u);
    sample.slope = 0.0;

    float distance;
    sample.radiance = lightSampleRadiance(light, surface.worldPosition, sample.samplePoint, sample.L, distance);
    const float NdotL = dot(surface.normal, sample.L);
    if (all(sample.radiance <= 0.0) || NdotL <= 0.0)
    {
        return sample;
    }
    sample.target = dot(sample.radiance, float3(0.2126, 0.7152, 0.0722)) * NdotL;
    sample.slope = shadowConeSlope(light, distance);
    return sample;
}

LightTerms shadeSampledLight(Surface surface, SampledLight sample, float3 scale)
{
    if (all(scale <= 0.0))
    {
        LightTerms none;
        none.diffuse = float3(0.0, 0.0, 0.0);
        none.specular = float3(0.0, 0.0, 0.0);
        return none;
    }
    LightTerms terms = shadeLightTerms(surface.normal, surface.viewDirection, sample.L, sample.radiance, surface.albedo, surface.metallic,
                                       surface.roughness, surface.reflectivity);
    terms.diffuse *= scale;
    terms.specular = analyticSpecular(gLights[sample.lightIndex], surface) * scale;
    return terms;
}

/// Marched lights ray each and sampled lights share one RIS pick
LightTerms shadeMarchedLights(Surface surface, float3 shadowOrigin, float4 motion, out Reservoir reservoir)
{
    reservoir.lightId = 0u;
    reservoir.W = 0.0;
    reservoir.u = float3(0.0, 0.0, 0.0);
    reservoir.M = 0u;

    LightTerms terms = shadeMarchedSuns(surface, shadowOrigin);

    const ClusterCell cluster = clusterCellAt(surface.viewSpaceZ, surface.pixel);
    if (cluster.index >= cluster.count)
    {
        return terms;
    }
    const uint stride = gClusterParams.clusterDims.w;
    const uint indexBase = cluster.index * stride;
    const uint lightCount = min(cluster.lightCount, stride);

    for (uint li = 0u; li < lightCount; ++li)
    {
        const GpuLight light = gLights[gLightIndexList[indexBase + li]];
        if (lightShadowMode(light) != kShadowModeMarched)
        {
            continue;
        }
        const float4 noise = blueNoise(surface.pixel, 1u + li);
        const float3 samplePoint = sampleLightShape(light, surface.worldPosition, noise.xyz);
        float3 L;
        float distance;
        const float3 radiance = lightSampleRadiance(light, surface.worldPosition, samplePoint, L, distance);
        if (all(radiance <= 0.0) || dot(surface.normal, L) <= 0.0)
        {
            continue;
        }
        const float meshShadow = meshShadowFactor(light, surface);
        if (meshShadow <= 0.0)
        {
            continue;
        }
        // Before the trace so the surface is not live across it
        const float3 diffuse = shadeLightTerms(surface.normal, surface.viewDirection, L, radiance, surface.albedo, surface.metallic,
                                               surface.roughness, surface.reflectivity).diffuse;
        const float3 specular = analyticSpecular(light, surface);
        const float3 visibility = min(meshShadow, marchedVisibility(shadowOrigin, samplePoint, shadowConeSlope(light, distance)));
        terms.diffuse += diffuse * visibility;
        terms.specular += specular * visibility;
    }

    // Uniform light and point candidates weighted by luminance times count then ReSTIR temporal reuse
    const uint candidates = gPush.lighting.y;
    if (lightCount == 0u || candidates == 0u)
    {
        return terms;
    }
    float weightSum = 0.0;
    uint sampleCount = candidates;
    SampledLight chosen;
    chosen.target = 0.0;
    for (uint k = 0u; k < candidates; ++k)
    {
        const float4 noise = blueNoise(surface.pixel, 64u + k);
        const uint pick = min(uint(noise.x * float(lightCount)), lightCount - 1u);
        const GpuLight light = gLights[gLightIndexList[indexBase + pick]];
        if (lightShadowMode(light) != kShadowModeSampled)
        {
            continue;
        }
        SampledLight candidate = evaluateSampledLight(surface, light, float3(noise.y, noise.z, frac(noise.x * 7.0 + noise.w)));
        candidate.lightIndex = gLightIndexList[indexBase + pick];
        const float weight = candidate.target * float(lightCount);
        if (weight <= 0.0)
        {
            continue;
        }
        weightSum += weight;
        if (noise.w * weightSum < weight)
        {
            chosen = candidate;
        }
    }

    if ((gPush.reuse.x & kReuseHistory) != 0u && motion.z <= 0.5)
    {
        const float2 size = gPush.viewport.xy;
        const float2 previousPixel = (surface.pixel / size - (motion.w > 0.5 ? motion.xy : float2(0.0, 0.0))) * size;
        const float angle = blueNoise(surface.pixel, 95u).x * 6.28318531;
        for (uint t = 0u; t < gPush.reuse.z; ++t)
        {
            const float2 offset = t == 0u ? float2(0.0, 0.0) : kReuseRadius * float2(cos(angle + float(t) * 2.39996323), sin(angle + float(t) * 2.39996323));
            const int2 tap = int2(floor(previousPixel + offset));
            if (any(tap < int2(0, 0)) || any(tap >= int2(size)))
            {
                continue;
            }
            const Reservoir previous = unpackReservoir(gPreviousReservoirs.Load(int3(tap, 0)));
            if (previous.W <= 0.0)
            {
                continue;
            }
            const uint previousM = min(previous.M, gPush.reuse.y);
            sampleCount += previousM;

            // Light ids persist across frames while indices do not
            const uint found = findSampledLight(previous.lightId, indexBase, lightCount);
            if (found == 0xFFFFFFFFu)
            {
                continue;
            }
            SampledLight candidate = evaluateSampledLight(surface, gLights[found], previous.u);
            candidate.lightIndex = found;
            const float weight = candidate.target * previous.W * float(previousM);
            if (weight <= 0.0)
            {
                continue;
            }
            weightSum += weight;
            if (blueNoise(surface.pixel, 96u + t).y * weightSum < weight)
            {
                chosen = candidate;
            }
        }
    }

    // The spatial pass traces the merged choice
    if (chosen.target > 0.0 && (gPush.reuse.x & kReuseSpatial) != 0u)
    {
        reservoir.lightId = chosen.lightId;
        reservoir.W = weightSum / (float(sampleCount) * chosen.target);
        reservoir.u = chosen.u;
        reservoir.M = sampleCount;
        return terms;
    }
    if (chosen.target > 0.0)
    {
        const float unbiasedWeight = weightSum / (float(sampleCount) * chosen.target);
        const float meshShadow = meshShadowFactor(gLights[chosen.lightIndex], surface);
        const LightTerms unshadowed = shadeSampledLight(surface, chosen, float3(unbiasedWeight, unbiasedWeight, unbiasedWeight));
        const float3 visibility = meshShadow > 0.0 ? min(meshShadow, marchedVisibility(shadowOrigin, chosen.samplePoint, chosen.slope))
                                               : float3(0.0, 0.0, 0.0);
        terms.diffuse += unshadowed.diffuse * visibility;
        terms.specular += unshadowed.specular * visibility;

        // Occluded samples are not reused
        reservoir.lightId = chosen.lightId;
        reservoir.W = any(visibility > 0.0) ? unbiasedWeight : 0.0;
        reservoir.u = chosen.u;
        reservoir.M = sampleCount;
    }
    return terms;
}


/// Neighbour reservoirs of this frame a pixel resamples and how far apart in pixels
static const uint kSpatialReservoirs = 3u;
static const float kSpatialRadius = 16.0;

// Mirrors the attachments of the voxel spatial scope
struct SpatialOutput
{
    float4 noisyDiffuse : SV_Target0;
    float4 noisySpecular : SV_Target1;
    uint4 reservoir : SV_Target2;
};

/// Merges neighbour reservoirs of this frame then traces the chosen light
/// A constant glassScene compiles the tinted trace out
SpatialOutput spatialReuse(VSOutput input, bool glassScene)
{
    SpatialOutput output;
    output.noisyDiffuse = float4(0.0, 0.0, 0.0, 0.0);
    output.noisySpecular = float4(0.0, 0.0, 0.0, 0.0);
    output.reservoir = uint4(0u, 0u, 0u, 0u);

    const int2 pixel = int2(input.position.xy);
    const int3 texel = int3(pixel, 0);
    const float4 albedo = gAlbedo.Load(texel);
    if (!isLitVoxel(albedo.a))
    {
        return output;
    }

    const float3 position = positionAt(pixel);
    const float3 faceNormal = decodeFaceNormal(gFaceNormal.Load(texel));
    const float3 sceneNormal = gSceneNormal.Load(texel).xyz;
    const float3 normal = dot(sceneNormal, sceneNormal) > 1e-4 ? normalize(sceneNormal) : faceNormal;
    const float4 material = gMaterial.Load(texel);
    const float3 toCamera = gCamera.position - position;

    Surface surface;
    surface.marchedShadows = true;
    surface.worldPosition = position;
    surface.normal = normal;
    surface.geometricNormal = faceNormal;
    surface.viewDirection = dot(toCamera, toCamera) > 1e-10 ? normalize(toCamera) : normal;
    surface.albedo = albedo.rgb;
    surface.metallic = material.r;
    surface.reflectivity = material.g;
    surface.roughness = clamp(material.b, 0.045, 1.0);
    surface.occlusion = 1.0;
    surface.viewSpaceZ = mul(gCamera.view, float4(position, 1.0)).z;
    surface.pixel = input.position.xy;

    const ClusterCell cluster = clusterCellAt(surface.viewSpaceZ, surface.pixel);
    if (cluster.index >= cluster.count)
    {
        return output;
    }
    const uint stride = gClusterParams.clusterDims.w;
    const uint indexBase = cluster.index * stride;
    const uint lightCount = min(cluster.lightCount, stride);

    float weightSum = 0.0;
    uint sampleCount = 0u;
    SampledLight chosen;
    chosen.target = 0.0;
    const float angle = blueNoise(surface.pixel, 140u).x * 6.28318531;
    const int2 size = int2(gPush.viewport.xy);
    for (uint t = 0u; t <= kSpatialReservoirs; ++t)
    {
        int2 tap = pixel;
        if (t > 0u)
        {
            const float tapAngle = angle + float(t) * 2.39996323;
            float s, c;
            sincos(tapAngle, s, c);
            const float distance = kSpatialRadius * sqrt((float(t) - 0.5) / float(kSpatialReservoirs));
            tap = int2(floor(input.position.xy + distance * float2(c, s)));
            float3 tapFace;
            if (any(tap < int2(0, 0)) || any(tap >= size) || !isLitVoxelAt(tap, tapFace))
            {
                continue;
            }
            // A neighbour on another surface chose for a point this pixel is not
            const float3 tapNormal = gSceneNormal.Load(int3(tap, 0)).xyz;
            if (dot(tapNormal, tapNormal) < 1e-4 || dot(normalize(tapNormal), normal) < 0.9)
            {
                continue;
            }
            const float plane = abs(dot(faceNormal, positionAt(tap) - position));
            if (plane > 0.02 * length(toCamera) + 0.05)
            {
                continue;
            }
        }

        const Reservoir candidateReservoir = unpackReservoir(gCandidateReservoirs.Load(int3(tap, 0)));
        if (candidateReservoir.W <= 0.0)
        {
            sampleCount += t == 0u ? candidateReservoir.M : 0u;
            continue;
        }
        sampleCount += candidateReservoir.M;

        const uint found = findSampledLight(candidateReservoir.lightId, indexBase, lightCount);
        if (found == 0xFFFFFFFFu)
        {
            continue;
        }
        SampledLight candidate = evaluateSampledLight(surface, gLights[found], candidateReservoir.u);
        candidate.lightIndex = found;
        const float weight = candidate.target * candidateReservoir.W * float(candidateReservoir.M);
        if (weight <= 0.0)
        {
            continue;
        }
        weightSum += weight;
        if (blueNoise(surface.pixel, 150u + t).z * weightSum < weight)
        {
            chosen = candidate;
        }
    }

    if (chosen.target <= 0.0 || sampleCount == 0u)
    {
        return output;
    }
    const float unbiasedWeight = weightSum / (float(sampleCount) * chosen.target);
    const float3 shadowOrigin = position + faceNormal * (kShadowOriginOffset * kVoxelSize);
    const float meshShadow = meshShadowFactor(gLights[chosen.lightIndex], surface);
    // Before the trace so the surface is not live across it
    const LightTerms unshadowed = shadeSampledLight(surface, chosen, float3(unbiasedWeight, unbiasedWeight, unbiasedWeight));
    Reservoir reservoir;
    reservoir.lightId = chosen.lightId;
    reservoir.u = chosen.u;
    reservoir.M = sampleCount;

    const float3 visibility = meshShadow > 0.0
                                  ? min(meshShadow, marchedVisibility(shadowOrigin, chosen.samplePoint, chosen.slope, glassScene))
                                  : float3(0.0, 0.0, 0.0);
    output.noisyDiffuse = float4(unshadowed.diffuse * visibility, 0.0);
    output.noisySpecular = float4(unshadowed.specular * visibility, 0.0);
    reservoir.W = any(visibility > 0.0) ? unbiasedWeight : 0.0;
    output.reservoir = packReservoir(reservoir);
    return output;
}

[shader("fragment")]
SpatialOutput fragmentSpatial(VSOutput input)
{
    return spatialReuse(input, true);
}

[shader("fragment")]
SpatialOutput fragmentSpatialOpaque(VSOutput input)
{
    return spatialReuse(input, false);
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
    output.noisyDiffuse = float4(0.0, 0.0, 0.0, 0.0);
    if (!isLitVoxel(albedo.a))
    {
        output.color = float4(albedo.rgb, 1.0);
        return output;
    }

    const float3 position = positionAt(pixel);
    const float3 normal = decodeFaceNormal(gFaceNormal.Load(texel));
    const float4 material = gMaterial.Load(texel);
    const float roughness = clamp(material.b, 0.045, 1.0);

    const bool traced = (gPush.reuse.x & kTracedSpecular) != 0u && material.g > 1e-3 &&
                        roughness <= float((gPush.reuse.x >> 8) & 0xffu) / 255.0;
    // SSR only reads roughness so zero reflectivity must look fully rough
    output.normalRoughness =
        encodeSceneNormal(normal, traced ? kTracedMark : lerp(1.0, roughness, material.g), specularF0(albedo.rgb, material.r));

    // The near plane clip can put the hit on the camera
    const float3 toCamera = gCamera.position - position;
    const float3 view = dot(toCamera, toCamera) > 1e-10 ? normalize(toCamera) : normal;

    Surface surface;
    surface.marchedShadows = true;
    surface.worldPosition = position;
    surface.normal = normal;
    surface.geometricNormal = normal;
    surface.viewDirection = view;
    surface.albedo = albedo.rgb;
    surface.metallic = material.r;
    surface.reflectivity = material.g;
    surface.roughness = roughness;
    surface.occlusion = 1.0;
    surface.viewSpaceZ = mul(gCamera.view, float4(position, 1.0)).z;
    surface.pixel = input.position.xy;

    // Glass scenes leave shadow mapped lights to fragmentMarched which tints them
    VoxelRayFreeFilter rayFree;
    rayFree.mapLights = (gPush.reuse.x & kSceneGlass) == 0u || isShadowDebugMode(gCamera.renderModePad.x);
    const ShadingResult lit = shadeSurfaceLighting(surface, rayFree);
    output.indirectDiffuse = float4(lit.indirectDiffuse, 1.0);
    if (lit.isDebugView)
    {
        output.color = float4(lit.color, 1.0);
        return output;
    }

    output.color = float4(lit.color + albedo.rgb * decodeEmissive(material.a), 1.0);
    if (traced)
    {
        output.color.rgb -= lit.ambientSpecular;
    }

    // Traced AO replaces SSAO so ambient diffuse moves into the filtered term
    if (gPush.lighting.z != 0u && gPush.reuse.w != 0u)
    {
        output.color.rgb -= lit.indirectDiffuse;
        output.noisyDiffuse = float4(select(albedo.rgb > 1e-3, lit.indirectDiffuse / max(albedo.rgb, 1e-3), float3(0.0, 0.0, 0.0)), 0.0);
        output.indirectDiffuse = float4(0.0, 0.0, 0.0, 1.0);
    }
    return output;
}

/// Mirrors the voxel marched scope where alpha 0 makes the premultiplied blend add
struct MarchedOutput
{
    /// Glass tinted shadow mapped lights added over fragmentMain
    float4 color : SV_Target0;
    float4 noisyDiffuse : SV_Target1;
    float4 noisySpecular : SV_Target2;
    /// packReservoir for next frame
    uint4 reservoir : SV_Target3;
};

/// Every voxel lighting ray so fragmentMain stays light on registers
[shader("fragment")]
MarchedOutput fragmentMarched(VSOutput input)
{
    MarchedOutput output;
    output.color = float4(0.0, 0.0, 0.0, 0.0);
    output.noisyDiffuse = float4(0.0, 0.0, 0.0, 0.0);
    output.noisySpecular = float4(0.0, 0.0, 0.0, 0.0);
    output.reservoir = uint4(0u, 0u, 0u, 0u);

    const int2 pixel = int2(input.position.xy);
    const int3 texel = int3(pixel, 0);
    const float4 albedo = gAlbedo.Load(texel);
    if (!isLitVoxel(albedo.a))
    {
        discard;
    }

    const float3 position = positionAt(pixel);
    const float3 faceNormal = decodeFaceNormal(gFaceNormal.Load(texel));
    const float3 sceneNormal = gSceneNormal.Load(texel).xyz;
    const float3 normal = dot(sceneNormal, sceneNormal) > 1e-4 ? normalize(sceneNormal) : faceNormal;
    const float4 material = gMaterial.Load(texel);
    const float3 toCamera = gCamera.position - position;

    Surface surface;
    surface.marchedShadows = true;
    surface.worldPosition = position;
    surface.normal = normal;
    surface.geometricNormal = faceNormal;
    surface.viewDirection = dot(toCamera, toCamera) > 1e-10 ? normalize(toCamera) : normal;
    surface.albedo = albedo.rgb;
    surface.metallic = material.r;
    surface.reflectivity = material.g;
    surface.roughness = clamp(material.b, 0.045, 1.0);
    surface.occlusion = 1.0;
    surface.viewSpaceZ = mul(gCamera.view, float4(position, 1.0)).z;
    surface.pixel = input.position.xy;

    // Debug views come from fragmentMain alone
    if (isShadowDebugMode(gCamera.renderModePad.x) || isSurfaceDebugMode(gCamera.renderModePad.x))
    {
        return output;
    }

    const float3 shadowOrigin = position + faceNormal * (kShadowOriginOffset * kVoxelSize);
    if ((gPush.reuse.x & kSceneGlass) != 0u)
    {
        VoxelGlassFilter glassFilter;
        glassFilter.origin = shadowOrigin;
        glassFilter.sunReach = float(gPush.reuse.x >> 16);
        glassFilter.sunSlope = 0.005;
        float3 direct = float3(0.0, 0.0, 0.0);
        float3 specular = float3(0.0, 0.0, 0.0);
        float debugMinShadow = 1.0;
        evaluateDirectLighting(surface, clusterCellAt(surface.viewSpaceZ, surface.pixel), glassFilter, direct, specular, debugMinShadow);
        output.color = float4(direct, 0.0);
    }

    if (gPush.lighting.z != 0u)
    {
        Reservoir reservoir;
        const LightTerms marched = shadeMarchedLights(surface, shadowOrigin, gMotion.Load(texel), reservoir);
        output.reservoir = packReservoir(reservoir);
        output.noisyDiffuse = float4(marched.diffuse, 0.0);
        output.noisySpecular = float4(marched.specular, 0.0);
    }
    return output;
}

/// Ambient rays alone so fragmentMarched keeps fewer live registers
[shader("fragment")]
float4 fragmentAmbient(VSOutput input) : SV_Target0
{
    const int2 pixel = int2(input.position.xy);
    const int3 texel = int3(pixel, 0);
    if (!isLitVoxel(gAlbedo.Load(texel).a) || isShadowDebugMode(gCamera.renderModePad.x) || isSurfaceDebugMode(gCamera.renderModePad.x))
    {
        discard;
    }

    const float3 position = positionAt(pixel);
    const float3 faceNormal = decodeFaceNormal(gFaceNormal.Load(texel));
    const float3 sceneNormal = gSceneNormal.Load(texel).xyz;
    const float3 normal = dot(sceneNormal, sceneNormal) > 1e-4 ? normalize(sceneNormal) : faceNormal;

    // Under the cascades the rays only add the contact detail their probe spacing blurs
    const float reach = float(gPush.reuse.w) * 0.01;
    const float contactReach = radianceCascadesCover(position, normal) ? radianceCascadesContactReach(position) : 0.0;
    const float3 shadowOrigin = position + faceNormal * (kShadowOriginOffset * kVoxelSize);
    const float occlusion = ambientOcclusion(normal, input.position.xy, shadowOrigin, contactReach > 0.0 ? min(reach, contactReach) : reach);
    // Premultiplied so the indirect fragmentMain left in noisy diffuse scales by occlusion
    return float4(0.0, 0.0, 0.0, 1.0 - occlusion);
}

/// Marched terms straight into colour when the denoiser is off
[shader("fragment")]
float4 fragmentMarchedComposite(VSOutput input) : SV_Target0
{
    const int3 texel = int3(int2(input.position.xy), 0);
    const float4 albedo = gAlbedo.Load(texel);
    if (!isLitVoxel(albedo.a))
    {
        discard;
    }
    return float4(albedo.rgb * gNoisyDiffuse.Load(texel).rgb + gNoisySpecular.Load(texel).rgb, 0.0);
}
