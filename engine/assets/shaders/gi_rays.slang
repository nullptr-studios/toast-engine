// Set 0 is the engine scene and set 1 the voxel storage with the scene trace
import voxel_dda;
import lighting;
import scene_trace;
import gi_common;
import gi_sky;
import trace_shading;
import fog_media;

[[vk::binding(0, 2)]]
Sampler2D gDepth;

[[vk::binding(1, 2)]]
Sampler2D gNormal;

[[vk::binding(2, 2)]]
StructuredBuffer<GiFrame> gFrame;

[[vk::binding(3, 2)]]
StructuredBuffer<uint> gTableKeys;

[[vk::binding(4, 2)]]
StructuredBuffer<uint> gTableValues;

[[vk::binding(5, 2)]]
StructuredBuffer<GiProbe> gPool;

/// x rays y first R2 index z child cursor w cursor the pixels of the probe take indices from
[[vk::binding(6, 2)]]
RWStructuredBuffer<uint4> gRayCounts;

/// Per direction xyz fixed point radiance w transmittance << 16 plus count
[[vk::binding(7, 2)]]
RWStructuredBuffer<uint4> gDeposits;

[[vk::binding(8, 2)]]
StructuredBuffer<uint> gParent;

[[vk::binding(9, 2)]]
StructuredBuffer<uint> gDepositSlots;

/// Lit HDR scene of this frame before any post pass
[[vk::binding(10, 2)]]
Sampler2D gSceneColour;

[[vk::binding(11, 2)]]
RWStructuredBuffer<uint> gCounters;

/// Read from the half the last trace wrote and appended to the half of this frame
[[vk::binding(12, 2)]]
RWStructuredBuffer<GiPoint> gPoints;

/// c0 tiles of the last merge until this frame merges after the trace
[[vk::binding(13, 2)]]
Sampler2D gIrradiance;

/// Per pixel x irradiance luminance last frame y smoothed change per frame z camera distance
[[vk::binding(14, 2)]]
RWStructuredBuffer<float4> gChange;

/// Off the surface in meters so the first voxel the ray leaves is not hit at t 0
static const float kGiRayOffset = 0.05;
static const float kGiMaxRadiance = 16384.0;
/// Meters off the plane of the hit face the visible surface may sit and still count as the same face
static const float kGiScreenPlane = 0.025;

/// One corner by trilinear weight skipping probes over half a cell behind the surface so light does not leak under
float3 giSecondaryIrradiance(GiFrame frame, float3 position, float3 normal, inout uint random)
{
    const uint lod = giSecondaryLod(frame, position);
    const GiCascadeLayout layout = frame.cascades[0];
    const float spacing = giSpacing(frame, lod, 0u);
    const float3 coord = (position + normal * (kGiSurfaceBias * spacing)) / spacing - 0.5;
    const int3 base = int3(floor(coord));
    const float3 upper = coord - float3(base);
    const float pick = giRandom(random);
    float cumulative = 0.0;
    uint first = 7u;
    for (uint corner = 0u; corner < 7u; ++corner)
    {
        cumulative += giNeighbourWeight(corner, upper);
        if (pick < cumulative)
        {
            first = corner;
            break;
        }
    }
    for (uint step = 0u; step < 8u; ++step)
    {
        const uint corner = (first + step) & 7u;
        const int3 cell = base + giNeighbourOffset(corner);
        if (giNeighbourWeight(corner, upper) <= 0.0 || dot((float3(cell) + 0.5) * spacing - position, normal) < -0.5 * spacing)
        {
            continue;
        }
        const uint key = giKey(cell, lod);
        uint slot = giHash(key) & layout.y;
        uint value = kGiEmpty;
        for (uint attempt = 0u; attempt < 32u; ++attempt)
        {
            const uint stored = gTableKeys[layout.x + slot];
            if (stored == key || stored == kGiEmpty)
            {
                value = stored == key ? gTableValues[layout.x + slot] : kGiEmpty;
                break;
            }
            slot = (slot + 1u) & layout.y;
        }
        if (value >= layout.w)
        {
            continue;
        }
        const uint previous = gPool[frame.frame.z * frame.pool.x + layout.z + value].previous;
        if (previous == kGiEmpty)
        {
            continue;
        }
        const uint tile = gPool[(1u - frame.frame.z) * frame.pool.x + layout.z + previous].deposit;
        if (tile == kGiEmpty)
        {
            continue;
        }
        return gIrradiance.SampleLevel(giTileUv(tile, normal), 0.0).rgb;
    }
    return float3(0.0, 0.0, 0.0);
}

/// Rare rays onto small bright emitters flicker their bucket without a ceiling
float3 giClampRay(GiFrame frame, float3 radiance)
{
    const float luminance = dot(radiance, float3(0.2126, 0.7152, 0.0722));
    return luminance > frame.limits.x ? radiance * (frame.limits.x / luminance) : radiance;
}

uint giFindDeposit(GiFrame frame, uint cascade, int3 cell, uint lod)
{
    const GiCascadeLayout layout = frame.cascades[cascade];
    const uint key = giKey(cell, lod);
    uint slot = giHash(key) & layout.y;
    for (uint attempt = 0u; attempt < 32u; ++attempt)
    {
        const uint stored = gTableKeys[layout.x + slot];
        if (stored == key)
        {
            const uint value = gTableValues[layout.x + slot];
            if (value >= layout.w)
            {
                return kGiEmpty;
            }
            const uint deposit = gPool[frame.frame.z * frame.pool.x + layout.z + value].deposit;
            return deposit == kGiEmpty ? kGiEmpty : frame.deposits[cascade].x + deposit;
        }
        if (stored == kGiEmpty)
        {
            return kGiEmpty;
        }
        slot = (slot + 1u) & layout.y;
    }
    return kGiEmpty;
}

/// False unless the pixel the hit projects to shows the same face
bool giScreenRadiance(GiFrame frame, float3 position, float3 normal, out float3 radiance)
{
    radiance = float3(0.0, 0.0, 0.0);
    const float3 toCamera = gCamera.position - position;
    if (dot(normal, toCamera) <= 0.0)
    {
        return false;
    }
    const float4 clip = mul(gCamera.jitteredViewProjection, float4(position, 1.0));
    if (clip.w <= 0.0)
    {
        return false;
    }
    const float2 ndc = clip.xy / clip.w;
    if (any(abs(ndc) >= 1.0))
    {
        return false;
    }
    const int2 pixel = int2((ndc * 0.5 + 0.5) * float2(frame.viewport.xy));
    const float depth = gDepth.Load(int3(pixel, 0)).r;
    if (giIsSky(depth))
    {
        return false;
    }
    // Same plane within a quarter voxel and close along it so an occluder in front or a parallel face behind fails
    const float distance = length(toCamera);
    const float3 visible = giWorldAt(frame, (float2(pixel) + 0.5) / float2(frame.viewport.xy), depth);
    const float3 offset = visible - position;
    if (abs(dot(offset, normal)) > kGiScreenPlane + 0.002 * distance || length(offset) > 0.2 + 0.02 * distance)
    {
        return false;
    }
    radiance = gSceneColour.Load(int3(pixel, 0)).rgb;
    return true;
}

float3 giHitRadiance(GiFrame frame, SceneHit hit, float3 origin, float3 direction, uint2 pixel, inout uint random, out bool onScreen)
{
    const float3 position = origin + direction * hit.t;
    const float3 normal = sceneHitNormal(hit, direction);
    float3 radiance = float3(0.0, 0.0, 0.0);
    onScreen = giScreenRadiance(frame, position, normal, radiance);
    if (onScreen)
    {
        return radiance;
    }

    const PaletteSample palette = samplePalette(gTraceVolumes[hit.volume].recordIndex, hit.material);
    // Shadow rays take the cone floor of the GI rays since their light is averaged over a whole bucket
    radiance = palette.albedo * palette.emissive + traceLocalLight(position, normal, palette.albedo, frame.rays.w, random);
    if (frame.secondary.y != 0u)
    {
        radiance += palette.albedo * giSecondaryIrradiance(frame, position, normal, random);
    }
    return radiance + traceSunLight(position, normal, palette.albedo, frame.rays.w, frame.rays.y, float2(pixel));
}

void giDeposit(GiFrame frame, uint cascade, uint deposit, float2 uv, float3 radiance, float transmittance)
{
    const uint first = gDepositSlots[deposit];
    if (first == kGiEmpty)
    {
        return;
    }
    const uint slot = first + giDirectionIndex(uv, cascade);
    if (any(radiance > 0.0))
    {
        const uint3 fixedPoint = uint3(clamp(radiance, 0.0, kGiMaxRadiance) * frame.rays.z + 0.5);
        InterlockedAdd(gDeposits[slot].x, fixedPoint.x);
        InterlockedAdd(gDeposits[slot].y, fixedPoint.y);
        InterlockedAdd(gDeposits[slot].z, fixedPoint.z);
    }
    InterlockedAdd(gDeposits[slot].w, (uint(saturate(transmittance) * kGiTransmittanceScale + 0.5) << 16) | 1u);
}

/// Fog dims each interval and the top cascade takes the sky
void giSplitRay(GiFrame frame, uint deposit, uint lod, float3 origin, float3 direction, bool hit, float distance, float3 radiance)
{
    const uint top = frame.frame.y - 1u;
    const float2 uv = giEncodeDirection(direction);
    float start = 0.0;
    for (uint cascade = 0u; cascade <= top && deposit != kGiEmpty; ++cascade)
    {
        const float end = giIntervalEnd(frame, lod, cascade);
        if (hit && distance <= end)
        {
            giDeposit(frame, cascade, deposit, uv, radiance * fogTransmittance(origin, direction, start, distance), 0.0);
            return;
        }
        const float through = fogTransmittance(origin, direction, start, end);
        if (cascade == top)
        {
            giDeposit(frame, cascade, deposit, uv, giSky(direction) * through, 0.0);
            return;
        }
        giDeposit(frame, cascade, deposit, uv, float3(0.0, 0.0, 0.0), through);
        deposit = gParent[deposit];
        start = end;
    }
}

[shader("compute")]
[numthreads(8, 8, 1)]
void traceMain(uint3 id : SV_DispatchThreadID)
{
    const GiFrame frame = gFrame[0];
    const uint2 pixel = id.xy * uint(frame.lod.w) + frame.viewport.zw;
    if (any(pixel >= frame.viewport.xy))
    {
        return;
    }
    const float depth = gDepth.Load(int3(pixel, 0)).r;
    if (giIsSky(depth))
    {
        return;
    }
    const float2 uv = (float2(pixel) + 0.5) / float2(frame.viewport.xy);
    const float3 world = giWorldAt(frame, uv, depth);
    const float3 normal = giSurfaceNormal(gNormal.Load(int3(pixel, 0)), world, frame.camera.xyz);
    bool overlap = false;
    const uint lod = giLod(frame, world, overlap);
    const uint deposit = giFindDeposit(frame, 0u, giSurfaceCell(frame, world, normal, lod, 0u), lod);
    if (deposit == kGiEmpty)
    {
        return;
    }

    // Directions flipped into the surface
    uint rayIndex = 0u;
    InterlockedAdd(gRayCounts[deposit].w, 1u, rayIndex);
    float3 direction = giDecodeDirection(giRayPoint(frame, gRayCounts[deposit].y + rayIndex));
    direction = dot(direction, normal) < 0.0 ? -direction : direction;

    const uint top = frame.frame.y - 1u;
    const uint reachLod = overlap ? lod + 1u : lod;
    const float3 origin = world + normal * kGiRayOffset;
    const SceneHit hit = traceScene(origin, direction, 0.0, giIntervalEnd(frame, reachLod, top), frame.rays.w);
    uint random = giHash(pixel.x ^ giHash(pixel.y ^ giHash(frame.frame.x)));
    bool onScreen = false;
    const float3 radiance =
        hit.hit ? giClampRay(frame, giHitRadiance(frame, hit, origin, direction, pixel, random, onScreen)) : float3(0.0, 0.0, 0.0);

    // A uniform share so every region seeds instead of dispatch order filling the buffer
    if (hit.hit && !onScreen && frame.secondary.y != 0u && giRandom(random) < asfloat(frame.secondary.z))
    {
        uint index = 0u;
        InterlockedAdd(gCounters[kGiCounterPoints + frame.frame.z], 1u, index);
        if (index < frame.secondary.w)
        {
            GiPoint point;
            point.position = origin + direction * hit.t;
            point.normal = giPackNormal(sceneHitNormal(hit, direction));
            gPoints[frame.frame.z * frame.secondary.x + index] = point;
        }
        else
        {
            InterlockedAdd(gCounters[kGiCounterPointsDropped], 1u);
        }
    }

    const bool primaryHit = hit.hit && hit.t <= giIntervalEnd(frame, lod, top);
    giSplitRay(frame, deposit, lod, origin, direction, primaryHit, hit.t, radiance);
    if (overlap)
    {
        const uint next = giFindDeposit(frame, 0u, giSurfaceCell(frame, world, normal, lod + 1u, 0u), lod + 1u);
        giSplitRay(frame, next, lod + 1u, origin, direction, hit.hit, hit.t, radiance);
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void pointTraceMain(uint3 id : SV_DispatchThreadID)
{
    const GiFrame frame = gFrame[0];
    if (id.x >= min(gCounters[kGiCounterPoints + 1u - frame.frame.z], frame.secondary.w))
    {
        return;
    }
    const GiPoint point = gPoints[(1u - frame.frame.z) * frame.secondary.x + id.x];
    const float3 normal = giUnpackNormal(point.normal);
    const uint lod = giSecondaryLod(frame, point.position);
    const uint deposit = giFindDeposit(frame, 0u, giSurfaceCell(frame, point.position, normal, lod, 0u), lod);
    if (deposit == kGiEmpty)
    {
        return;
    }

    uint rayIndex = 0u;
    InterlockedAdd(gRayCounts[deposit].w, 1u, rayIndex);
    float3 direction = giDecodeDirection(giRayPoint(frame, gRayCounts[deposit].y + rayIndex));
    direction = dot(direction, normal) < 0.0 ? -direction : direction;

    const float3 origin = point.position + normal * kGiRayOffset;
    const float end = giIntervalEnd(frame, lod, frame.frame.y - 1u);
    const SceneHit hit = traceScene(origin, direction, 0.0, end, frame.rays.w);
    uint random = giHash(id.x ^ giHash(frame.frame.x ^ 0x5bd1e995u));
    bool onScreen = false;
    const uint2 pixel = uint2(id.x & 63u, id.x >> 6);
    const float3 radiance =
        hit.hit ? giClampRay(frame, giHitRadiance(frame, hit, origin, direction, pixel, random, onScreen)) : float3(0.0, 0.0, 0.0);
    giSplitRay(frame, deposit, lod, origin, direction, hit.hit, hit.t, radiance);
}

float3 giTileIrradiance(GiFrame frame, float3 world, float3 normal, uint lod, out float total)
{
    const GiCascadeLayout layout = frame.cascades[0];
    const float spacing = giSpacing(frame, lod, 0u);
    const float3 coord = (world + normal * (kGiSurfaceBias * spacing)) / spacing - 0.5;
    const int3 base = int3(floor(coord));
    const float3 upper = coord - float3(base);
    float3 irradiance = float3(0.0, 0.0, 0.0);
    total = 0.0;
    for (uint corner = 0u; corner < 8u; ++corner)
    {
        const uint key = giKey(base + giNeighbourOffset(corner), lod);
        uint slot = giHash(key) & layout.y;
        uint value = kGiEmpty;
        for (uint attempt = 0u; attempt < 32u; ++attempt)
        {
            const uint stored = gTableKeys[layout.x + slot];
            if (stored == key || stored == kGiEmpty)
            {
                value = stored == key ? gTableValues[layout.x + slot] : kGiEmpty;
                break;
            }
            slot = (slot + 1u) & layout.y;
        }
        if (value >= layout.w)
        {
            continue;
        }
        const uint tile = gPool[frame.frame.z * frame.pool.x + layout.z + value].deposit;
        if (tile == kGiEmpty)
        {
            continue;
        }
        const float weight = giNeighbourWeight(corner, upper);
        irradiance += weight * gIrradiance.SampleLevel(giTileUv(tile, normal), 0.0).rgb;
        total += weight;
    }
    return total > 0.0 ? irradiance / total : irradiance;
}

/// For the change debug view
[shader("compute")]
[numthreads(8, 8, 1)]
void changeMain(uint3 id : SV_DispatchThreadID)
{
    const GiFrame frame = gFrame[0];
    if (any(id.xy >= frame.viewport.xy))
    {
        return;
    }
    const uint index = id.y * frame.viewport.x + id.x;
    const float depth = gDepth.Load(int3(id.xy, 0)).r;
    if (giIsSky(depth))
    {
        gChange[index] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    const float2 uv = (float2(id.xy) + 0.5) / float2(frame.viewport.xy);
    const float3 world = giWorldAt(frame, uv, depth);
    const float3 normal = giSurfaceNormal(gNormal.Load(int3(id.xy, 0)), world, frame.camera.xyz);
    bool overlap = false;
    const uint lod = giLod(frame, world, overlap);
    float total = 0.0;
    const float3 irradiance = giTileIrradiance(frame, world, normal, lod, total);
    const float luminance = dot(irradiance, float3(0.2126, 0.7152, 0.0722));
    const float4 last = gChange[index];
    // A silhouette pixel lands on a different surface each jittered frame so it only counts while its surface holds
    const float distance = length(world - frame.camera.xyz);
    if (abs(distance - last.z) > 0.01 * distance)
    {
        gChange[index] = float4(luminance, last.y, distance, 0.0);
        return;
    }
    const float change = total > 0.0 ? abs(luminance - last.x) / max(max(luminance, last.x), 1e-4) : 0.0;
    gChange[index] = float4(luminance, lerp(last.y, change, 0.1), distance, 0.0);
}
