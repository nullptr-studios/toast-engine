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

/// Merged radiance of every cascade above c0 at first slot / 4 as half rgb
[[vk::binding(6, 1)]]
RWStructuredBuffer<uint2> gMerged;

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

/// Mirrors gi_probes.slang gDirty
[[vk::binding(11, 1)]]
StructuredBuffer<float4> gDirty;

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

GiParents giParents(GiFrame frame, uint cascade, uint deposit, out int4 probeCell)
{
    const GiProbe probe = gPool[frame.frame.z * frame.pool.x + frame.cascades[cascade].z + gDepositPool[deposit]];
    probeCell = probe.cell;
    int3 base = int3(0, 0, 0);
    float3 upper = float3(0.0, 0.0, 0.0);
    giParentNeighbourhood(probe.cell.xyz, base, upper);
    const uint4 low = gNeighbours[deposit * 2u];
    const uint4 high = gNeighbours[deposit * 2u + 1u];

    GiParents parents;
    parents.total = 0.0;
    // Branch free so the eight slot loads overlap
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner)
    {
        const uint neighbour = corner < 4u ? low[corner] : high[corner - 4u];
        const uint slot = gDepositSlots[neighbour == kGiEmpty ? 0u : neighbour];
        const uint first = neighbour == kGiEmpty ? kGiEmpty : slot;
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
    // Loads first so the eight overlap and a missing parent weighs 0
    uint2 merged[8];
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner)
    {
        merged[corner] = gMerged[parents.merged[corner] + direction];
    }
    float3 radiance = float3(0.0, 0.0, 0.0);
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner)
    {
        if (parents.weight[corner] > 0.0)
        {
            radiance += parents.weight[corner] *
                        float3(giHalfToFloat(merged[corner].x & 0xffffu), giHalfToFloat(merged[corner].x >> 16), giHalfToFloat(merged[corner].y));
        }
    }
    return radiance / parents.total;
}

struct GiBucket
{
    uint4 value;
    GiHistory old;
};

/// Bounded since a capture replay may not restore last frame pool
bool giCarried(GiFrame frame, uint cascade, uint2 source)
{
    return source.x != kGiEmpty && source.x <= frame.pool.w - frame.deposits[cascade].w;
}

/// Reads the old bucket unconditionally so it loads alongside the deposit
GiBucket giLoadBucket(GiFrame frame, uint first, uint direction, uint2 source, bool carried)
{
    GiBucket bucket;
    bucket.value = gDeposits[first + direction];
    bucket.old = gHistory[(1u - frame.frame.z) * frame.pool.w + (carried ? source.x : 0u) + direction];
    return bucket;
}

bool giBucketTouchesChange(GiFrame frame, uint cascade, int4 cell, uint direction)
{
    const uint count = uint(gDirty[0].x);
    if (count == 0u)
    {
        return false;
    }
    const uint lod = uint(cell.w);
    const float spacing = giSpacing(frame, lod, cascade);
    const float3 centre = (float3(cell.xyz) + 0.5) * spacing;
    const float start = (cascade == 0u ? 0.0 : giIntervalEnd(frame, lod, cascade - 1u)) - spacing;
    const float end = giIntervalEnd(frame, lod, cascade) + spacing;
    const float3 axis = giDirectionOf(direction, cascade);
    // Half again the bucket half angle since rays jitter across the whole bucket
    const float cosine = max(1.0 - 2.0 / float(frame.deposits[cascade].w), 0.05);
    const float spread = 1.5 * sqrt(1.0 - cosine * cosine) / cosine;
    for (uint i = 0u; i < count; ++i)
    {
        const float3 low = gDirty[1u + 2u * i].xyz;
        const float3 high = gDirty[2u + 2u * i].xyz;
        const float radius = 0.5 * length(high - low);
        const float3 offset = 0.5 * (low + high) - centre;
        const float along = dot(offset, axis);
        if (along + radius < start || along - radius > end)
        {
            continue;
        }
        if (length(offset - axis * along) <= radius + spacing + max(along, 0.0) * spread)
        {
            return true;
        }
    }
    return false;
}

/// keepMain flags whole probes and only buckets looking at the change drop history
float giBucketLimit(GiFrame frame, uint cascade, int4 cell, uint direction, uint2 source)
{
    const float limit = asfloat(source.y);
    if (limit >= kGiMaxHistory)
    {
        return limit;
    }
    return giBucketTouchesChange(frame, cascade, cell, direction) ? limit : kGiMaxHistory;
}

/// Unreached buckets decay only to a slower fading floor of limits.y rays
GiHistory giAccumulate(GiFrame frame, GiBucket bucket, uint2 source, bool carried, float limit)
{
    float weight = float(bucket.value.w & 0xffffu);
    float3 radiance = float3(bucket.value.xyz) / frame.rays.z;
    float transmittance = float(bucket.value.w >> 16) / kGiTransmittanceScale;
    float rays = 0.0;
    const float4 old = giHistoryBucket(bucket.old, rays);
    const float held = min(rays * frame.limits.z, max(frame.limits.y - weight, 0.0));
    const float kept = carried ? min(max(rays * frame.jitter.z, held), limit) : 0.0;
    radiance += old.rgb * kept;
    transmittance += old.a * kept;
    weight += kept;
    const float inverse = weight > 0.0 ? 1.0 / weight : 0.0;
    return giPackHistory(radiance * inverse, transmittance * inverse, weight);
}

/// Every 64th @p item feeds the depth counter
void giStoreBucket(GiFrame frame, uint cascade, uint first, uint direction, uint item, GiHistory history)
{
    gHistory[frame.frame.z * frame.pool.w + first + direction] = history;
    if ((item & 63u) == 0u)
    {
        InterlockedAdd(gCounters[kGiCounterDepth + cascade], uint(history.rays * 16.0 + 0.5));
    }
}

/// L = R + T * L above for one bucket
float3 giMergeBucket(GiFrame frame, uint cascade, uint direction, bool top, GiParents parents, GiHistory history)
{
    float rays = 0.0;
    const float4 bucket = giHistoryBucket(history, rays);
    const float3 upper = top ? giSky(giDirectionOf(direction, cascade)) : giUpper(parents, direction);
    return bucket.rgb + bucket.a * upper;
}

/// One cascade above c0 per dispatch from the top down accumulating its buckets on the way
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
        const uint2 source = gHistorySource[deposit];
        const bool carried = giCarried(frame, cascade, source);
        int4 probeCell = int4(0, 0, 0, 0);
        const GiParents parents = giParents(frame, cascade, deposit, probeCell);
        // Stores last since the compiler cannot move a load past a store that may alias it
        GiBucket buckets[4];
        [unroll]
        for (uint child = 0u; child < 4u; ++child)
        {
            buckets[child] = giLoadBucket(frame, first, output * 4u + child, source, carried);
        }
        GiHistory histories[4];
        float3 radiance = float3(0.0, 0.0, 0.0);
        [unroll]
        for (uint child = 0u; child < 4u; ++child)
        {
            histories[child] =
                giAccumulate(frame, buckets[child], source, carried, giBucketLimit(frame, cascade, probeCell, output * 4u + child, source));
            radiance += giMergeBucket(frame, cascade, output * 4u + child, top, parents, histories[child]);
        }
        [unroll]
        for (uint child = 0u; child < 4u; ++child)
        {
            const uint direction = output * 4u + child;
            giStoreBucket(frame, cascade, first, direction, (item / outputs) * deposits.w + direction, histories[child]);
        }
        radiance *= 0.25;
        gMerged[first / 4u + output] =
            uint2(giFloatToHalf(radiance.r) | (giFloatToHalf(radiance.g) << 16), giFloatToHalf(radiance.b));
    }
}

groupshared float3 gsRadiance[2][kGiC0Directions];
groupshared bool gsWrite[2];
groupshared GiParents gsParents[2];
groupshared int4 gsCell[2];

/// Two c0 deposit probes per 8 by 8 group so every thread merges one bucket and then writes one texel of each tile
[shader("compute")]
[numthreads(kGiTileSize, kGiTileSize, 1)]
void irradianceMain(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID, uint index : SV_GroupIndex)
{
    const GiFrame frame = gFrame[0];
    const uint count = giDepositCount(frame, 0u);
    const uint pair = group.y * kGiAtlasTilesPerRow + group.x * 2u;
    // Uniform across the group so the barrier below stays legal
    if (pair >= count)
    {
        return;
    }

    const uint side = index / kGiC0Directions;
    const uint direction = index % kGiC0Directions;
    const uint tile = pair + side;
    const uint deposit = frame.deposits[0].x + tile;
    const uint first = tile < count ? gDepositSlots[deposit] : kGiEmpty;
    if (direction == 0u)
    {
        const bool willWrite = first != kGiEmpty;
        gsWrite[side] = willWrite;
        if (willWrite)
        {
            int4 cell = int4(0, 0, 0, 0);
            gsParents[side] = giParents(frame, 0u, deposit, cell);
            gsCell[side] = cell;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (gsWrite[side])
    {
        const bool top = frame.frame.y == 1u;
        const uint2 source = gHistorySource[deposit];
        const bool carried = giCarried(frame, 0u, source);
        const GiBucket bucket = giLoadBucket(frame, first, direction, source, carried);
        const GiParents parents = gsParents[side];
        const GiHistory history = giAccumulate(frame, bucket, source, carried, giBucketLimit(frame, 0u, gsCell[side], direction, source));
        gsRadiance[side][direction] = giMergeBucket(frame, 0u, direction, top, parents, history);
        giStoreBucket(frame, 0u, first, direction, tile * kGiC0Directions + direction, history);
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
    float3 irradiance[2] = { float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0) };
    float weight = 0.0;
    for (uint d = 0u; d < kGiC0Directions; ++d)
    {
        const float cosine = max(dot(normal, kGiC0DirectionsTable[d]), 0.0);
        irradiance[0] += gsRadiance[0][d] * cosine;
        irradiance[1] += gsRadiance[1][d] * cosine;
        weight += cosine;
    }
    [unroll]
    for (uint s = 0u; s < 2u; ++s)
    {
        if (gsWrite[s])
        {
            gIrradiance[giTileOrigin(pair + s) + thread.xy] = float4(weight > 0.0 ? irradiance[s] / weight : irradiance[s], 1.0);
        }
    }
}
