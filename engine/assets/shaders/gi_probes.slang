import gi_common;

[[vk::binding(0, 0)]]
Sampler2D gDepth;

[[vk::binding(1, 0)]]
StructuredBuffer<GiFrame> gFrame;

[[vk::binding(9, 0)]]
Sampler2D gNormal;

/// One open addressing table per cascade rebuilt every frame
[[vk::binding(2, 0)]]
RWStructuredBuffer<uint> gTableKeys;
[[vk::binding(3, 0)]]
RWStructuredBuffer<uint> gTableFlags;
/// Index in the previous pool of a carried probe
[[vk::binding(4, 0)]]
RWStructuredBuffer<uint> gTablePrevious;
/// Index in the pool written this frame
[[vk::binding(5, 0)]]
RWStructuredBuffer<uint> gTableValues;
[[vk::binding(6, 0)]]
RWStructuredBuffer<int4> gTableCells;

/// Two pools by parity
[[vk::binding(7, 0)]]
RWStructuredBuffer<GiProbe> gPool;

/// Mirrors renderer::GiPass counters
[[vk::binding(8, 0)]]
RWStructuredBuffer<uint> gCounters;

/// Pool slot of each deposit probe
[[vk::binding(10, 0)]]
RWStructuredBuffer<uint> gDepositPool;
/// Deposit probe of the parent or kGiEmpty
[[vk::binding(11, 0)]]
RWStructuredBuffer<uint> gParent;
/// x rays y first R2 index z cursor its children take blocks from w cursor its pixels take indices from
[[vk::binding(12, 0)]]
RWStructuredBuffer<uint4> gRayCounts;
/// Per direction xyz fixed point radiance w transmittance << 16 plus count
[[vk::binding(13, 0)]]
RWStructuredBuffer<uint4> gDeposits;
/// First direction slot of each deposit probe or kGiEmpty when the slots ran out
[[vk::binding(14, 0)]]
RWStructuredBuffer<uint> gDepositSlots;
/// The 8 parent cascade deposit probes around each deposit probe as two uint4 in giNeighbourOffset order
[[vk::binding(15, 0)]]
RWStructuredBuffer<uint4> gNeighbours;
/// x first history slot of the probe last frame or kGiEmpty y asfloat rays of history it may keep
[[vk::binding(16, 0)]]
RWStructuredBuffer<uint2> gHistorySource;
/// Mirrors renderer::GiPass dirty boxes with the count in [0].x then world min and max per changed region
[[vk::binding(17, 0)]]
StructuredBuffer<float4> gDirty;
/// Two halves by pool parity
[[vk::binding(18, 0)]]
StructuredBuffer<GiPoint> gPoints;

struct GiPush
{
    uint cascade;
    /// slotsMain hands out to probes seen this frame when 0 and to the carried ones after when 1
    uint carried;
};

[[vk::push_constant]]
GiPush gPush;

static const uint kGiGroup = 64u;
/// Carried probes may sit this far past their LOD shell and stay well inside the 512 cell key window
static const float kGiShellSlack = 1.5;
static const uint kGiMaxProbes = 32u;

void giInsert(GiFrame frame, uint cascade, int3 cell, uint lod, uint flags, uint previous)
{
    const GiCascadeLayout layout = frame.cascades[cascade];
    const uint key = giKey(cell, lod);
    uint slot = giHash(key) & layout.y;
    for (uint attempt = 0u; attempt < kGiMaxProbes; ++attempt)
    {
        const uint index = layout.x + slot;
        uint original = 0u;
        InterlockedCompareExchange(gTableKeys[index], kGiEmpty, key, original);
        if (original == kGiEmpty || original == key)
        {
            if (original == kGiEmpty)
            {
                gTableCells[index] = int4(cell, int(lod));
                if (previous != kGiEmpty)
                {
                    gTablePrevious[index] = previous;
                }
            }
            if (flags != 0u)
            {
                InterlockedOr(gTableFlags[index], flags);
            }
            return;
        }
        slot = (slot + 1u) & layout.y;
    }
    InterlockedAdd(gCounters[kGiCounterTableFull], 1u);
}

[shader("compute")]
[numthreads(kGiGroup, 1, 1)]
void clearMain(uint3 id : SV_DispatchThreadID)
{
    const GiFrame frame = gFrame[0];
    const uint index = id.x;
    if (index == 0u)
    {
        for (uint c = 0u; c < kGiMaxCascades; ++c)
        {
            gCounters[frame.frame.z * 8u + c] = 0u;
            gCounters[kGiCounterSeen + c] = 0u;
        }
        gCounters[kGiCounterTableFull] = 0u;
        gCounters[kGiCounterPoolFull] = 0u;
        gCounters[kGiCounterDepositFull] = 0u;
        gCounters[kGiCounterRays] = 0u;
        gCounters[kGiCounterSlots] = 0u;
        gCounters[kGiCounterInvalidated] = 0u;
        gCounters[kGiCounterPoints + frame.frame.z] = 0u;
        gCounters[kGiCounterPointsDropped] = 0u;
        gCounters[kGiCounterFresh] = 0u;
        for (uint d = 0u; d < kGiMaxCascades; ++d)
        {
            gCounters[kGiCounterDepth + d] = 0u;
        }
    }
    if (index >= frame.pool.y)
    {
        return;
    }
    gTableKeys[index] = kGiEmpty;
    gTableFlags[index] = 0u;
    gTablePrevious[index] = kGiEmpty;
    gTableValues[index] = kGiEmpty;
}

[shader("compute")]
[numthreads(kGiGroup, 1, 1)]
void carryMain(uint3 id : SV_DispatchThreadID)
{
    const GiFrame frame = gFrame[0];
    const uint cascade = gPush.cascade;
    const GiCascadeLayout layout = frame.cascades[cascade];
    const uint parity = 1u - frame.frame.z;
    const uint count = min(gCounters[parity * 8u + cascade], layout.w);
    if (frame.frame.w == 0u || id.x >= count)
    {
        return;
    }
    const GiProbe probe = gPool[parity * frame.pool.x + layout.z + id.x];
    if (frame.frame.x - probe.lastSeen > uint(frame.lod.z))
    {
        return;
    }
    // Outside its LOD shell around the camera now so the modulo key cannot alias a probe a whole window away
    const uint lod = uint(probe.cell.w);
    const float3 centre = (float3(probe.cell.xyz) + 0.5) * giSpacing(frame, lod, cascade);
    const float3 offset = abs(centre - frame.camera.xyz);
    if (max(offset.x, max(offset.y, offset.z)) > kGiShellSlack * frame.lod.x * float(1u << (lod + 1u)))
    {
        return;
    }
    giInsert(frame, cascade, probe.cell.xyz, uint(probe.cell.w), 0u, id.x);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void pixelMain(uint3 id : SV_DispatchThreadID)
{
    const GiFrame frame = gFrame[0];
    const uint step = uint(frame.lod.w);
    const uint2 pixel = id.xy * step + frame.viewport.zw;
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
    giInsert(frame, 0u, giSurfaceCell(frame, world, normal, lod, 0u), lod, kGiFlagSeen, kGiEmpty);
    if (overlap)
    {
        giInsert(frame, 0u, giSurfaceCell(frame, world, normal, lod + 1u, 0u), lod + 1u, kGiFlagSeen, kGiEmpty);
    }
}

uint giPointCount(GiFrame frame)
{
    return min(gCounters[kGiCounterPoints + 1u - frame.frame.z], frame.secondary.x);
}

[shader("compute")]
[numthreads(kGiGroup, 1, 1)]
void pointMain(uint3 id : SV_DispatchThreadID)
{
    const GiFrame frame = gFrame[0];
    if (id.x >= giPointCount(frame))
    {
        return;
    }
    const GiPoint point = gPoints[(1u - frame.frame.z) * frame.secondary.x + id.x];
    const float3 normal = giUnpackNormal(point.normal);
    const uint lod = giSecondaryLod(frame, point.position);
    giInsert(frame, 0u, giSurfaceCell(frame, point.position, normal, lod, 0u), lod, kGiFlagSeen, kGiEmpty);
}

[shader("compute")]
[numthreads(kGiGroup, 1, 1)]
void parentMain(uint3 id : SV_DispatchThreadID)
{
    const GiFrame frame = gFrame[0];
    const uint cascade = gPush.cascade;
    const GiCascadeLayout layout = frame.cascades[cascade];
    if (id.x > layout.y)
    {
        return;
    }
    const uint index = layout.x + id.x;
    if (gTableKeys[index] == kGiEmpty || (gTableFlags[index] & kGiFlagSeen) == 0u)
    {
        return;
    }
    const int4 cell = gTableCells[index];
    // Arithmetic shift so negative cells round down like floor
    giInsert(frame, cascade + 1u, cell.xyz >> 1, uint(cell.w), kGiFlagSeen, kGiEmpty);
}

[shader("compute")]
[numthreads(kGiGroup, 1, 1)]
void compactMain(uint3 id : SV_DispatchThreadID)
{
    const GiFrame frame = gFrame[0];
    const uint cascade = gPush.cascade;
    const GiCascadeLayout layout = frame.cascades[cascade];
    if (id.x > layout.y)
    {
        return;
    }
    const uint index = layout.x + id.x;
    if (gTableKeys[index] == kGiEmpty)
    {
        return;
    }

    uint slot = 0u;
    InterlockedAdd(gCounters[frame.frame.z * 8u + cascade], 1u, slot);
    if (slot >= layout.w)
    {
        InterlockedAdd(gCounters[kGiCounterPoolFull], 1u);
        return;
    }

    const bool seen = (gTableFlags[index] & kGiFlagSeen) != 0u;
    const uint previous = gTablePrevious[index];
    const uint oldBase = (1u - frame.frame.z) * frame.pool.x + layout.z;
    const uint lastSeen = seen || previous == kGiEmpty ? frame.frame.x : gPool[oldBase + previous].lastSeen;

    // Probes a pixel touched get deposit storage first
    uint deposit = kGiEmpty;
    if (seen)
    {
        InterlockedAdd(gCounters[kGiCounterSeen + cascade], 1u, deposit);
        if (deposit >= frame.deposits[cascade].y)
        {
            InterlockedAdd(gCounters[kGiCounterDepositFull], 1u);
            deposit = kGiEmpty;
        }
        else
        {
            gDepositPool[frame.deposits[cascade].x + deposit] = slot;
        }
    }

    GiProbe probe;
    probe.cell = gTableCells[index];
    probe.lastSeen = lastSeen;
    probe.previous = previous;
    probe.deposit = deposit;
    probe.slots = kGiEmpty;
    gPool[frame.frame.z * frame.pool.x + layout.z + slot] = probe;
    gTableValues[index] = slot;
}

uint giFindSlot(GiFrame frame, uint cascade, int3 cell, uint lod)
{
    const GiCascadeLayout layout = frame.cascades[cascade];
    const uint key = giKey(cell, lod);
    uint slot = giHash(key) & layout.y;
    for (uint attempt = 0u; attempt < kGiMaxProbes; ++attempt)
    {
        const uint stored = gTableKeys[layout.x + slot];
        if (stored == key)
        {
            const uint value = gTableValues[layout.x + slot];
            return value < layout.w ? value : kGiEmpty;
        }
        if (stored == kGiEmpty)
        {
            return kGiEmpty;
        }
        slot = (slot + 1u) & layout.y;
    }
    return kGiEmpty;
}

uint giFindDeposit(GiFrame frame, uint cascade, int3 cell, uint lod)
{
    const uint slot = giFindSlot(frame, cascade, cell, lod);
    if (slot == kGiEmpty)
    {
        return kGiEmpty;
    }
    const uint deposit = gPool[frame.frame.z * frame.pool.x + frame.cascades[cascade].z + slot].deposit;
    return deposit == kGiEmpty ? kGiEmpty : frame.deposits[cascade].x + deposit;
}

uint giDepositCount(GiFrame frame, uint cascade)
{
    return min(gCounters[kGiCounterSeen + cascade], frame.deposits[cascade].y);
}

/// Padded a cell since the rays start anywhere on the surfaces inside it
bool giTouchesChange(GiFrame frame, uint cascade, int4 cell)
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
    for (uint i = 0u; i < count; ++i)
    {
        const float3 low = gDirty[1u + 2u * i].xyz;
        const float3 high = gDirty[2u + 2u * i].xyz;
        const float nearest = length(max(max(low - centre, centre - high), 0.0));
        const float farthest = length(max(abs(centre - low), abs(centre - high)));
        if (nearest <= end && farthest >= start)
        {
            return true;
        }
    }
    return false;
}

/// After compact so running out never costs a visible probe
[shader("compute")]
[numthreads(kGiGroup, 1, 1)]
void keepMain(uint3 id : SV_DispatchThreadID)
{
    const GiFrame frame = gFrame[0];
    const uint cascade = gPush.cascade;
    const GiCascadeLayout layout = frame.cascades[cascade];
    if (id.x >= min(gCounters[frame.frame.z * 8u + cascade], layout.w))
    {
        return;
    }
    const uint index = frame.frame.z * frame.pool.x + layout.z + id.x;
    const GiProbe probe = gPool[index];
    if (probe.deposit != kGiEmpty || frame.frame.x - probe.lastSeen > kGiKeepFrames)
    {
        return;
    }
    uint deposit = 0u;
    InterlockedAdd(gCounters[kGiCounterSeen + cascade], 1u, deposit);
    if (deposit >= frame.deposits[cascade].y)
    {
        InterlockedAdd(gCounters[kGiCounterDepositFull], 1u);
        return;
    }
    gDepositPool[frame.deposits[cascade].x + deposit] = id.x;
    gPool[index].deposit = deposit;
}

/// Top cascade first on seen probes then again on carried ones so carried probes run out first
[shader("compute")]
[numthreads(kGiGroup, 1, 1)]
void slotsMain(uint3 id : SV_DispatchThreadID)
{
    const GiFrame frame = gFrame[0];
    const uint cascade = gPush.cascade;
    if (id.x >= giDepositCount(frame, cascade))
    {
        return;
    }
    const uint4 deposits = frame.deposits[cascade];
    const uint deposit = deposits.x + id.x;
    const uint index = frame.frame.z * frame.pool.x + frame.cascades[cascade].z + gDepositPool[deposit];
    if ((gPool[index].lastSeen != frame.frame.x) != (gPush.carried != 0u))
    {
        return;
    }
    gRayCounts[deposit] = uint4(0u, 0u, 0u, 0u);
    gParent[deposit] = kGiEmpty;
    gNeighbours[deposit * 2u] = uint4(kGiEmpty, kGiEmpty, kGiEmpty, kGiEmpty);
    gNeighbours[deposit * 2u + 1u] = uint4(kGiEmpty, kGiEmpty, kGiEmpty, kGiEmpty);

    uint first = 0u;
    InterlockedAdd(gCounters[kGiCounterSlots], deposits.w, first);
    if (first + deposits.w > frame.pool.w)
    {
        // Every lookup now misses it so it gets no rays and its children merge without it
        InterlockedAdd(gCounters[kGiCounterDepositFull], 1u);
        gDepositSlots[deposit] = kGiEmpty;
        gPool[index].deposit = kGiEmpty;
        return;
    }
    gDepositSlots[deposit] = first;
    gPool[index].slots = first;
    const GiProbe probe = gPool[index];
    uint history = kGiEmpty;
    if (probe.previous != kGiEmpty)
    {
        history = gPool[(1u - frame.frame.z) * frame.pool.x + frame.cascades[cascade].z + probe.previous].slots;
    }
    if (history == kGiEmpty)
    {
        InterlockedAdd(gCounters[kGiCounterFresh], 1u);
    }
    float kept = kGiMaxHistory;
    if (history != kGiEmpty && giTouchesChange(frame, cascade, probe.cell))
    {
        kept = frame.jitter.w;
        InterlockedAdd(gCounters[kGiCounterInvalidated], 1u);
    }
    gHistorySource[deposit] = uint2(history, asuint(kept));
}

[shader("compute")]
[numthreads(kGiGroup, 1, 1)]
void depositClearMain(uint3 id : SV_DispatchThreadID)
{
    const GiFrame frame = gFrame[0];
    const uint used = min(gCounters[kGiCounterSlots], frame.pool.w);
    for (uint slot = id.x; slot < used; slot += kGiStrideThreads)
    {
        gDeposits[slot] = uint4(0u, 0u, 0u, 0u);
    }
}

[shader("compute")]
[numthreads(kGiGroup, 1, 1)]
void linkMain(uint3 id : SV_DispatchThreadID)
{
    const GiFrame frame = gFrame[0];
    const uint cascade = gPush.cascade;
    if (id.x >= giDepositCount(frame, cascade))
    {
        return;
    }
    const uint deposit = frame.deposits[cascade].x + id.x;
    if (gDepositSlots[deposit] == kGiEmpty)
    {
        return;
    }
    const GiProbe probe = gPool[frame.frame.z * frame.pool.x + frame.cascades[cascade].z + gDepositPool[deposit]];
    const uint lod = uint(probe.cell.w);
    int3 base = int3(0, 0, 0);
    float3 upper = float3(0.0, 0.0, 0.0);
    giParentNeighbourhood(probe.cell.xyz, base, upper);
    uint neighbours[8];
    for (uint corner = 0u; corner < 8u; ++corner)
    {
        neighbours[corner] = giFindDeposit(frame, cascade + 1u, base + giNeighbourOffset(corner), lod);
    }
    gNeighbours[deposit * 2u] = uint4(neighbours[0], neighbours[1], neighbours[2], neighbours[3]);
    gNeighbours[deposit * 2u + 1u] = uint4(neighbours[4], neighbours[5], neighbours[6], neighbours[7]);
    gParent[deposit] = neighbours[giParentCorner(probe.cell.xyz)];
}

uint giPixelDeposit(GiFrame frame, uint2 pixel)
{
    const float depth = gDepth.Load(int3(pixel, 0)).r;
    if (giIsSky(depth))
    {
        return kGiEmpty;
    }
    const float2 uv = (float2(pixel) + 0.5) / float2(frame.viewport.xy);
    const float3 world = giWorldAt(frame, uv, depth);
    const float3 normal = giSurfaceNormal(gNormal.Load(int3(pixel, 0)), world, frame.camera.xyz);
    bool overlap = false;
    const uint lod = giLod(frame, world, overlap);
    return giFindDeposit(frame, 0u, giSurfaceCell(frame, world, normal, lod, 0u), lod);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void countMain(uint3 id : SV_DispatchThreadID)
{
    const GiFrame frame = gFrame[0];
    const uint2 pixel = id.xy * uint(frame.lod.w) + frame.viewport.zw;
    if (any(pixel >= frame.viewport.xy))
    {
        return;
    }
    const uint deposit = giPixelDeposit(frame, pixel);
    if (deposit != kGiEmpty)
    {
        InterlockedAdd(gRayCounts[deposit].x, 1u);
    }
}

[shader("compute")]
[numthreads(kGiGroup, 1, 1)]
void pointCountMain(uint3 id : SV_DispatchThreadID)
{
    const GiFrame frame = gFrame[0];
    if (id.x >= giPointCount(frame))
    {
        return;
    }
    const GiPoint point = gPoints[(1u - frame.frame.z) * frame.secondary.x + id.x];
    const float3 normal = giUnpackNormal(point.normal);
    const uint lod = giSecondaryLod(frame, point.position);
    const uint deposit = giFindDeposit(frame, 0u, giSurfaceCell(frame, point.position, normal, lod, 0u), lod);
    if (deposit != kGiEmpty)
    {
        InterlockedAdd(gRayCounts[deposit].x, 1u);
    }
}

/// Cascade by cascade upward so a parent holds the rays of all its children
[shader("compute")]
[numthreads(kGiGroup, 1, 1)]
void propagateMain(uint3 id : SV_DispatchThreadID)
{
    const GiFrame frame = gFrame[0];
    const uint cascade = gPush.cascade;
    if (id.x >= giDepositCount(frame, cascade))
    {
        return;
    }
    const uint deposit = frame.deposits[cascade].x + id.x;
    const uint parent = gParent[deposit];
    const uint rays = gRayCounts[deposit].x;
    if (parent != kGiEmpty && rays > 0u)
    {
        InterlockedAdd(gRayCounts[parent].x, rays);
    }
}

/// Top down so the rays of every probe are one contiguous R2 run
[shader("compute")]
[numthreads(kGiGroup, 1, 1)]
void offsetMain(uint3 id : SV_DispatchThreadID)
{
    const GiFrame frame = gFrame[0];
    const uint cascade = gPush.cascade;
    if (id.x >= giDepositCount(frame, cascade))
    {
        return;
    }
    const uint deposit = frame.deposits[cascade].x + id.x;
    const uint rays = gRayCounts[deposit].x;
    const uint parent = cascade + 1u < frame.frame.y ? gParent[deposit] : kGiEmpty;
    uint offset = 0u;
    if (parent == kGiEmpty)
    {
        InterlockedAdd(gCounters[kGiCounterRays], rays, offset);
    }
    else
    {
        uint local = 0u;
        InterlockedAdd(gRayCounts[parent].z, rays, local);
        offset = gRayCounts[parent].y + local;
    }
    gRayCounts[deposit].y = offset;
}
