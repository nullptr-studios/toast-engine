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

/// Pixels countMain found with a deposit packed x | y << 16
[[vk::binding(15, 2)]]
StructuredBuffer<uint> gSpawn;

/// Off screen hits of pixel rays then of point rays from giPackHit for shadeMain
[[vk::binding(16, 2)]]
RWStructuredBuffer<uint4> gHits;

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

/// Radiance of a ray hit that is not on screen
float3 giOffscreenRadiance(GiFrame frame, SceneHit hit, float3 position, float3 normal, uint2 pixel, inout uint random)
{
    const PaletteSample palette = samplePalette(gTraceVolumes[hit.volume].recordIndex, hit.material);
    // Shadow rays take the cone floor of the GI rays since their light is averaged over a whole bucket
    float3 radiance = palette.albedo * palette.emissive + traceLocalLight(position, normal, palette.albedo, frame.rays.w, random);
    if (frame.secondary.y != 0u)
    {
        radiance += palette.albedo * giSecondaryIrradiance(frame, position, normal, random);
    }
    return radiance + traceSunLight(position, normal, palette.albedo, frame.rays.w, frame.rays.y, float2(pixel));
}

/// x source y R2 index z asuint(t) w volume | axisCode << 22 | material << 24 with axisCode 3 for no axis
uint4 giPackHit(uint source, uint rayPoint, SceneHit hit)
{
    const uint axisCode = hit.axis < 0 ? 3u : uint(hit.axis);
    const uint packed = (hit.volume & kTraceInstanceVolume) | (axisCode << 22) | ((hit.material & 0xFFu) << 24);
    return uint4(source, rayPoint, asuint(hit.t), packed);
}

/// Drops the voxel since shading reads only the volume axis and material
SceneHit giUnpackHit(uint4 packed)
{
    const uint axisCode = (packed.w >> 22) & 3u;
    SceneHit hit;
    hit.hit = true;
    hit.t = asfloat(packed.z);
    hit.volume = packed.w & kTraceInstanceVolume;
    hit.voxel = int3(0, 0, 0);
    hit.axis = axisCode == 3u ? -1 : int(axisCode);
    hit.material = packed.w >> 24;
    return hit;
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
    const bool hasFog = gCamera.fogMedium.x > 0.0 || fogVolumeCount() > 0u;
    for (uint cascade = 0u; cascade <= top && deposit != kGiEmpty; ++cascade)
    {
        const float end = giIntervalEnd(frame, lod, cascade);
        if (hit && distance <= end)
        {
            const float through = hasFog ? fogTransmittance(origin, direction, start, distance) : 1.0;
            giDeposit(frame, cascade, deposit, uv, radiance * through, 0.0);
            return;
        }
        const float through = hasFog ? fogTransmittance(origin, direction, start, end) : 1.0;
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

/// One probe ray of a pixel or a secondary point before its direction is picked
struct GiRay
{
    float3 origin;
    float3 direction;
    float3 surfaceNormal;
    /// End of the traceScene interval
    float reach;
    uint deposit;
    uint lod;
    /// kGiEmpty when the pixel has no overlap or no deposit in the next lod
    uint overlapDeposit;
    uint rayPoint;
    uint2 pixel;
    uint random;
};

/// False for sky or a pixel without a deposit with direction and rayPoint left unset
bool giPixelRayAt(GiFrame frame, uint2 pixel, out GiRay ray)
{
    ray.origin = float3(0.0, 0.0, 0.0);
    ray.direction = float3(0.0, 0.0, 0.0);
    ray.surfaceNormal = float3(0.0, 0.0, 0.0);
    ray.reach = 0.0;
    ray.deposit = kGiEmpty;
    ray.lod = 0u;
    ray.overlapDeposit = kGiEmpty;
    ray.rayPoint = 0u;
    ray.pixel = pixel;
    ray.random = 0u;
    const float depth = gDepth.Load(int3(pixel, 0)).r;
    if (giIsSky(depth))
    {
        return false;
    }
    const float2 uv = (float2(pixel) + 0.5) / float2(frame.viewport.xy);
    const float3 world = giWorldAt(frame, uv, depth);
    const float3 normal = giSurfaceNormal(gNormal.Load(int3(pixel, 0)), world, frame.camera.xyz);
    bool overlap = false;
    const uint lod = giLod(frame, world, overlap);
    const uint deposit = giFindDeposit(frame, 0u, giSurfaceCell(frame, world, normal, lod, 0u), lod);
    if (deposit == kGiEmpty)
    {
        return false;
    }
    ray.surfaceNormal = normal;
    ray.lod = lod;
    ray.deposit = deposit;
    ray.overlapDeposit = overlap ? giFindDeposit(frame, 0u, giSurfaceCell(frame, world, normal, lod + 1u, 0u), lod + 1u) : kGiEmpty;
    ray.reach = giIntervalEnd(frame, overlap ? lod + 1u : lod, frame.frame.y - 1u);
    ray.origin = world + normal * kGiRayOffset;
    ray.random = giHash(pixel.x ^ giHash(pixel.y ^ giHash(frame.frame.x)));
    return true;
}

/// False for a point without a deposit with direction and rayPoint left unset
bool giPointRayAt(GiFrame frame, uint index, out GiRay ray)
{
    ray.origin = float3(0.0, 0.0, 0.0);
    ray.direction = float3(0.0, 0.0, 0.0);
    ray.surfaceNormal = float3(0.0, 0.0, 0.0);
    ray.reach = 0.0;
    ray.deposit = kGiEmpty;
    ray.lod = 0u;
    ray.overlapDeposit = kGiEmpty;
    ray.rayPoint = 0u;
    ray.pixel = uint2(index & 63u, index >> 6);
    ray.random = 0u;
    const GiPoint point = gPoints[(1u - frame.frame.z) * frame.secondary.x + index];
    const float3 normal = giUnpackNormal(point.normal);
    const uint lod = giSecondaryLod(frame, point.position);
    const uint deposit = giFindDeposit(frame, 0u, giSurfaceCell(frame, point.position, normal, lod, 0u), lod);
    if (deposit == kGiEmpty)
    {
        return false;
    }
    ray.surfaceNormal = normal;
    ray.lod = lod;
    ray.deposit = deposit;
    ray.reach = giIntervalEnd(frame, lod, frame.frame.y - 1u);
    ray.origin = point.position + normal * kGiRayOffset;
    ray.random = giHash(index ^ giHash(frame.frame.x ^ 0x5bd1e995u));
    return true;
}

/// Directions flipped into the surface
void giAimRay(GiFrame frame, uint rayPoint, inout GiRay ray)
{
    ray.rayPoint = rayPoint;
    const float3 direction = giDecodeDirection(giRayPoint(frame, rayPoint));
    ray.direction = dot(direction, ray.surfaceNormal) < 0.0 ? -direction : direction;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void traceMain(uint3 id : SV_DispatchThreadID)
{
    const GiFrame frame = gFrame[0];
    if (id.x >= min(gCounters[kGiCounterSpawn], frame.queues.x))
    {
        return;
    }
    const uint packed = gSpawn[id.x];
    const uint2 pixel = uint2(packed & 0xFFFFu, packed >> 16u);
    GiRay ray;
    if (!giPixelRayAt(frame, pixel, ray))
    {
        return;
    }

    uint rayIndex = 0u;
    InterlockedAdd(gRayCounts[ray.deposit].w, 1u, rayIndex);
    giAimRay(frame, gRayCounts[ray.deposit].y + rayIndex, ray);

    const uint top = frame.frame.y - 1u;
    const SceneHit hit = traceScene(ray.origin, ray.direction, 0.0, ray.reach, frame.rays.w);
    float3 radiance = float3(0.0, 0.0, 0.0);
    if (hit.hit)
    {
        const float3 position = ray.origin + ray.direction * hit.t;
        const float3 normal = sceneHitNormal(hit, ray.direction);
        if (!giScreenRadiance(frame, position, normal, radiance))
        {
            // A uniform share so every region seeds instead of dispatch order filling the buffer
            if (frame.secondary.y != 0u && giRandom(ray.random) < asfloat(frame.secondary.z))
            {
                uint index = 0u;
                InterlockedAdd(gCounters[kGiCounterPoints + frame.frame.z], 1u, index);
                if (index < frame.secondary.w)
                {
                    GiPoint point;
                    point.position = position;
                    point.normal = giPackNormal(normal);
                    gPoints[frame.frame.z * frame.secondary.x + index] = point;
                }
                else
                {
                    InterlockedAdd(gCounters[kGiCounterPointsDropped], 1u);
                }
            }
            // shadeMain deposits it
            uint slot = 0u;
            InterlockedAdd(gCounters[kGiCounterPixelHits], 1u, slot);
            if (slot < frame.queues.y)
            {
                gHits[slot] = giPackHit(pixel.x | (pixel.y << 16u), ray.rayPoint, hit);
            }
            return;
        }
        radiance = giClampRay(frame, radiance);
    }

    const bool primaryHit = hit.hit && hit.t <= giIntervalEnd(frame, ray.lod, top);
    giSplitRay(frame, ray.deposit, ray.lod, ray.origin, ray.direction, primaryHit, hit.t, radiance);
    if (ray.overlapDeposit != kGiEmpty)
    {
        giSplitRay(frame, ray.overlapDeposit, ray.lod + 1u, ray.origin, ray.direction, hit.hit, hit.t, radiance);
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
    GiRay ray;
    if (!giPointRayAt(frame, id.x, ray))
    {
        return;
    }

    uint rayIndex = 0u;
    InterlockedAdd(gRayCounts[ray.deposit].w, 1u, rayIndex);
    giAimRay(frame, gRayCounts[ray.deposit].y + rayIndex, ray);

    const SceneHit hit = traceScene(ray.origin, ray.direction, 0.0, ray.reach, frame.rays.w);
    float3 radiance = float3(0.0, 0.0, 0.0);
    if (hit.hit)
    {
        const float3 position = ray.origin + ray.direction * hit.t;
        if (!giScreenRadiance(frame, position, sceneHitNormal(hit, ray.direction), radiance))
        {
            // shadeMain deposits it
            uint slot = 0u;
            InterlockedAdd(gCounters[kGiCounterPointHits], 1u, slot);
            if (slot < frame.queues.z - frame.queues.y)
            {
                gHits[frame.queues.y + slot] = giPackHit(id.x, ray.rayPoint, hit);
            }
            return;
        }
        radiance = giClampRay(frame, radiance);
    }
    giSplitRay(frame, ray.deposit, ray.lod, ray.origin, ray.direction, hit.hit, hit.t, radiance);
}

/// Off screen hits of both trace kernels in full warps
[shader("compute")]
[numthreads(64, 1, 1)]
void shadeMain(uint3 id : SV_DispatchThreadID)
{
    const GiFrame frame = gFrame[0];
    const uint pixelHits = min(gCounters[kGiCounterPixelHits], frame.queues.y);
    const bool pixelRay = id.x < pixelHits;
    const uint pointIndex = id.x - pixelHits;
    if (!pixelRay && pointIndex >= min(gCounters[kGiCounterPointHits], frame.queues.z - frame.queues.y))
    {
        return;
    }
    const uint4 packed = gHits[pixelRay ? id.x : frame.queues.y + pointIndex];
    GiRay ray;
    const bool found =
        pixelRay ? giPixelRayAt(frame, uint2(packed.x & 0xFFFFu, packed.x >> 16u), ray) : giPointRayAt(frame, packed.x, ray);
    if (!found)
    {
        return;
    }
    giAimRay(frame, packed.y, ray);
    const SceneHit hit = giUnpackHit(packed);
    const float3 position = ray.origin + ray.direction * hit.t;
    const float3 normal = sceneHitNormal(hit, ray.direction);
    // Salted so the light pick is independent of the seeding draw of traceMain
    uint random = giHash(ray.random ^ 0x68bc21ebu);
    const uint top = frame.frame.y - 1u;
    const bool primaryHit = !pixelRay || hit.t <= giIntervalEnd(frame, ray.lod, top);
    const float3 radiance = giOffscreenRadiance(frame, hit, position, normal, ray.pixel, random);

    // Reread so the frame is not live across the traces
    const GiFrame after = gFrame[0];
    const float3 clamped = giClampRay(after, radiance);
    giSplitRay(after, ray.deposit, ray.lod, ray.origin, ray.direction, primaryHit, hit.t, clamped);
    if (ray.overlapDeposit != kGiEmpty)
    {
        giSplitRay(after, ray.overlapDeposit, ray.lod + 1u, ray.origin, ray.direction, true, hit.t, clamped);
    }
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
        const float weight = giNeighbourWeight(corner, upper);
        if (weight <= 0.0)
        {
            continue;
        }
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
