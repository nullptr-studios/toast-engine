// Set 0 is the engine scene only for the sky above the top cascade
import lighting;
import gi_common;
import gi_sky;

[[vk::binding(0, 1)]]
StructuredBuffer<GiFrame> gFrame;

[[vk::binding(1, 1)]]
StructuredBuffer<GiProbe> gPool;

[[vk::binding(2, 1)]]
StructuredBuffer<uint> gDepositPool;

[[vk::binding(3, 1)]]
StructuredBuffer<uint> gDepositSlots;

[[vk::binding(4, 1)]]
StructuredBuffer<uint4> gNeighbours;

[[vk::binding(5, 1)]]
StructuredBuffer<uint4> gDeposits;

/// Every cascade above c0 merged and averaged down to the directions of the cascade below at its first slot / 4
[[vk::binding(6, 1)]]
RWStructuredBuffer<float4> gMerged;

[[vk::binding(7, 1)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> gIrradiance;

[[vk::binding(8, 1)]]
RWStructuredBuffer<uint> gCounters;

/// Two halves by pool parity
[[vk::binding(9, 1)]]
RWStructuredBuffer<GiHistory> gHistory;

[[vk::binding(10, 1)]]
StructuredBuffer<uint2> gHistorySource;

struct GiMergePush
{
    uint cascade;
};

[[vk::push_constant]]
GiMergePush gPush;

static const uint kGiGroup = 64u;

uint giDepositCount(GiFrame frame, uint cascade)
{
    return min(gCounters[kGiCounterSeen + cascade], frame.deposits[cascade].y);
}

struct GiParents
{
    uint merged[8];
    float weight[8];
    float total;
};

GiParents giParents(GiFrame frame, uint cascade, uint deposit)
{
    const GiProbe probe = gPool[frame.frame.z * frame.pool.x + frame.cascades[cascade].z + gDepositPool[deposit]];
    int3 base = int3(0, 0, 0);
    float3 upper = float3(0.0, 0.0, 0.0);
    giParentNeighbourhood(probe.cell.xyz, base, upper);
    const uint4 low = gNeighbours[deposit * 2u];
    const uint4 high = gNeighbours[deposit * 2u + 1u];

    GiParents parents;
    parents.total = 0.0;
    for (uint corner = 0u; corner < 8u; ++corner)
    {
        const uint neighbour = corner < 4u ? low[corner] : high[corner - 4u];
        const uint first = neighbour == kGiEmpty ? kGiEmpty : gDepositSlots[neighbour];
        parents.merged[corner] = first == kGiEmpty ? 0u : first / 4u;
        parents.weight[corner] = first == kGiEmpty ? 0.0 : giNeighbourWeight(corner, upper);
        parents.total += parents.weight[corner];
    }
    return parents;
}

/// Black when no parent got rays since a guess of sky would leak through walls
float3 giUpper(GiParents parents, uint direction)
{
    if (parents.total <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }
    float3 radiance = float3(0.0, 0.0, 0.0);
    for (uint corner = 0u; corner < 8u; ++corner)
    {
        if (parents.weight[corner] > 0.0)
        {
            radiance += parents.weight[corner] * gMerged[parents.merged[corner] + direction].rgb;
        }
    }
    return radiance / parents.total;
}

/// L = R + T * L above for one bucket
float3 giMergeBucket(GiFrame frame, uint cascade, uint first, uint direction, bool top, GiParents parents)
{
    float rays = 0.0;
    const float4 bucket = giHistoryBucket(gHistory[frame.frame.z * frame.pool.w + first + direction], rays);
    const float3 upper = top ? giSky(giDirectionOf(direction, cascade)) : giUpper(parents, direction);
    return bucket.rgb + bucket.a * upper;
}

/// Unreached buckets keep their mean and decay only to a slower fading floor of limits.y rays so rare directions hold
[shader("compute")]
[numthreads(kGiGroup, 1, 1)]
void accumulateMain(uint3 id : SV_DispatchThreadID)
{
    const GiFrame frame = gFrame[0];
    const uint cascade = gPush.cascade;
    const uint4 deposits = frame.deposits[cascade];
    const uint total = giDepositCount(frame, cascade) * deposits.w;
    const uint current = frame.frame.z * frame.pool.w;
    const uint previous = (1u - frame.frame.z) * frame.pool.w;
    for (uint item = id.x; item < total; item += kGiStrideThreads)
    {
        const uint deposit = deposits.x + item / deposits.w;
        const uint direction = item % deposits.w;
        const uint first = gDepositSlots[deposit];
        if (first == kGiEmpty)
        {
            continue;
        }
        const uint4 value = gDeposits[first + direction];
        float weight = float(value.w & 0xffffu);
        float3 radiance = float3(value.xyz) / frame.rays.z;
        float transmittance = float(value.w >> 16);

        // Bounded too since the slot comes from last frame pool which a capture replay may not restore
        const uint2 source = gHistorySource[deposit];
        if (source.x != kGiEmpty && source.x <= frame.pool.w - deposits.w)
        {
            float rays = 0.0;
            const float4 old = giHistoryBucket(gHistory[previous + source.x + direction], rays);
            const float held = min(rays * frame.limits.z, max(frame.limits.y - weight, 0.0));
            const float kept = min(max(rays * frame.jitter.z, held), asfloat(source.y));
            radiance += old.rgb * kept;
            transmittance += old.a * kept;
            weight += kept;
        }
        const float inverse = weight > 0.0 ? 1.0 / weight : 0.0;
        gHistory[current + first + direction] = giPackHistory(radiance * inverse, transmittance * inverse, weight);
        if ((item & 63u) == 0u)
        {
            InterlockedAdd(gCounters[kGiCounterDepth + cascade], uint(min(weight, kGiMaxHistory) * 16.0 + 0.5));
        }
    }
}

/// One cascade above c0 per dispatch from the top down
[shader("compute")]
[numthreads(kGiGroup, 1, 1)]
void mergeMain(uint3 id : SV_DispatchThreadID)
{
    const GiFrame frame = gFrame[0];
    const uint cascade = gPush.cascade;
    const uint4 deposits = frame.deposits[cascade];
    const uint outputs = deposits.w / 4u;
    const uint total = giDepositCount(frame, cascade) * outputs;
    const bool top = cascade + 1u >= frame.frame.y;
    for (uint item = id.x; item < total; item += kGiStrideThreads)
    {
        const uint deposit = deposits.x + item / outputs;
        const uint output = item % outputs;
        const uint first = gDepositSlots[deposit];
        if (first == kGiEmpty)
        {
            continue;
        }
        const GiParents parents = giParents(frame, cascade, deposit);
        float3 radiance = float3(0.0, 0.0, 0.0);
        for (uint child = 0u; child < 4u; ++child)
        {
            radiance += giMergeBucket(frame, cascade, first, output * 4u + child, top, parents);
        }
        gMerged[first / 4u + output] = float4(radiance * 0.25, 1.0);
    }
}

groupshared float3 gsRadiance[kGiC0Directions];

/// One 8 by 8 group per c0 deposit probe and one texel per thread
[shader("compute")]
[numthreads(kGiTileSize, kGiTileSize, 1)]
void irradianceMain(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID, uint index : SV_GroupIndex)
{
    const GiFrame frame = gFrame[0];
    const uint tile = group.y * kGiAtlasTilesPerRow + group.x;
    // Both returns are uniform across the group so the barrier below stays legal
    if (tile >= giDepositCount(frame, 0u))
    {
        return;
    }
    const uint deposit = frame.deposits[0].x + tile;
    const uint first = gDepositSlots[deposit];
    if (first == kGiEmpty)
    {
        return;
    }

    if (index < kGiC0Directions)
    {
        const bool top = frame.frame.y == 1u;
        const GiParents parents = giParents(frame, 0u, deposit);
        gsRadiance[index] = giMergeBucket(frame, 0u, first, index, top, parents);
    }
    GroupMemoryBarrierWithGroupSync();

    // Border texels hold the interior texel mirrored across the octahedral seam so bilinear taps wrap correctly
    const uint last = kGiTileSize - 1u;
    const bool edgeX = thread.x == 0u || thread.x == last;
    const bool edgeY = thread.y == 0u || thread.y == last;
    uint2 source = thread.xy;
    if (edgeX && edgeY)
    {
        source = uint2(thread.x == 0u ? kGiTileInterior : 1u, thread.y == 0u ? kGiTileInterior : 1u);
    }
    else if (edgeY)
    {
        source = uint2(last - thread.x, thread.y == 0u ? 1u : kGiTileInterior);
    }
    else if (edgeX)
    {
        source = uint2(thread.x == 0u ? 1u : kGiTileInterior, last - thread.y);
    }
    const float3 normal = giOctDecode((float2(source) - 0.5) / float(kGiTileInterior));

    // Normalized by the discrete cosine sum so uniform radiance comes out exact with only 32 buckets
    float3 irradiance = float3(0.0, 0.0, 0.0);
    float weight = 0.0;
    for (uint direction = 0u; direction < kGiC0Directions; ++direction)
    {
        const float cosine = max(dot(normal, giDirectionOf(direction, 0u)), 0.0);
        irradiance += gsRadiance[direction] * cosine;
        weight += cosine;
    }
    gIrradiance[giTileOrigin(tile) + thread.xy] = float4(weight > 0.0 ? irradiance / weight : irradiance, 1.0);
}
