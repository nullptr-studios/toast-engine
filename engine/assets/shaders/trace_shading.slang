// Direct light at a traced hit shared by GI rays and traced specular
import voxel_dda;
import lighting;
import scene_trace;
import gi_common;

/// Meters off the hit face shadow rays start
static const float kTraceHitOffset = 0.05;
static const uint kTraceMaxLocalLights = 64u;

float traceLightWeight(GpuLight light, float3 position, float3 normal)
{
    float3 L;
    float distance;
    const float3 radiance = lightSampleRadiance(light, position, light.worldPosRange.xyz, L, distance);
    return dot(radiance, float3(0.2126, 0.7152, 0.0722)) * max(dot(normal, L), 0.0);
}

/// Weighted and uniform reservoirs mixed half and half so a weak visible light beside a blocked strong one never fireflies
float3 traceLocalLight(float3 position, float3 normal, float3 albedo, float coneSlope, inout uint random)
{
    const uint count = min(gClusterParams.lightCounts.x, kTraceMaxLocalLights);
    float total = 0.0;
    float lit = 0.0;
    uint weighted = kGiEmpty;
    float weightedWeight = 0.0;
    uint even = kGiEmpty;
    float evenWeight = 0.0;
    for (uint li = 0u; li < count; ++li)
    {
        const float weight = traceLightWeight(gLights[li], position, normal);
        if (weight <= 0.0)
        {
            continue;
        }
        total += weight;
        lit += 1.0;
        random = giHash(random + 0x9e3779b9u);
        if (float(random & 0xffffu) / 65536.0 * total < weight)
        {
            weighted = li;
            weightedWeight = weight;
        }
        if (float(random >> 16) / 65536.0 * lit < 1.0)
        {
            even = li;
            evenWeight = weight;
        }
    }
    if (lit <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    const bool byWeight = giRandom(random) < 0.5;
    const uint chosen = byWeight ? weighted : even;
    const float odds = 0.5 * (byWeight ? weightedWeight : evenWeight) / total + 0.5 / lit;
    const GpuLight light = gLights[chosen];
    const float3 u = float3(giRandom(random), giRandom(random), giRandom(random));
    const float3 samplePoint = sampleLightShape(light, position, u);
    float3 L;
    float distance;
    const float3 radiance = lightSampleRadiance(light, position, samplePoint, L, distance);
    const float cosine = dot(normal, L);
    if (cosine <= 0.0 || all(radiance <= 0.0))
    {
        return float3(0.0, 0.0, 0.0);
    }
    const float3 origin = position + normal * kTraceHitOffset;
    const float visibility = traceVisibility(origin, samplePoint - origin, coneSlope, false).x;
    return albedo / kPi * radiance * cosine * visibility / odds;
}

/// The caster sun reads its cascade at @p pixel and a marched sun also marches voxels up to @p sunReach meters
float3 traceSunLight(float3 position, float3 normal, float3 albedo, float coneSlope, float sunReach, float2 pixel)
{
    float3 radiance = float3(0.0, 0.0, 0.0);
    const uint casterIndex = uint(gCamera.shadowParams.x) - 1u;
    for (uint di = 0u; di < gCamera.directionalLightCountPad.x; ++di)
    {
        const DirectionalLightGpu light = gCamera.directionalLights[di];
        const float3 L = normalize(-light.direction.xyz);
        const float cosine = dot(normal, L);
        if (cosine <= 0.0)
        {
            continue;
        }
        const bool mapped = gCamera.shadowParams.x >= 0.5 && di == casterIndex;
        float shadow = 1.0;
        if (mapped)
        {
            const float viewDepth = abs(mul(gCamera.view, float4(position, 1.0)).z);
            shadow = sampleDirectionalShadow(position, normal, L, viewDepth, pixel);
        }
        if ((!mapped || light.direction.w > 0.0) && shadow > 0.0)
        {
            shadow = min(shadow, traceVisibility(position + normal * kTraceHitOffset, L * sunReach, coneSlope, false).x);
        }
        radiance += albedo / kPi * light.colorIntensity.rgb * light.colorIntensity.a * cosine * shadow;
    }
    return radiance;
}
