//   count     each cell is classified once and a surviving cell takes the next slot of its list
//   allocate  one thread turns the counts into offsets and indirect commands
//   write     each surviving cell stores itself at its list offset plus slot
//   mesh      one group per near cell turns the exposed faces of its bricks into greedy quads
//   late      one thread per retest cell against the new pyramid then the late near cells mesh
// Early phases test the previous pyramid with the previous camera and transforms so a wrong guess only costs a late test
import voxel_dda;
import depth_convention;

/// Mirrors renderer::VoxelPass::CellCullPush
struct CellCullPush
{
    float4x4 viewProjection;
    /// xyz world camera position and w the near plane distance
    float4 cameraNear;
    uint totalCells;
    uint instanceCount;
    uint phase;
    uint quadCapacity;
    /// Cells closer than this many voxels are meshed and 0 marches every cell
    float nearVoxels;
    /// Pyramid levels and 0 skips the occlusion test
    uint hizLevels;
    /// Scene extent in pixels the pyramid was built over
    uint hizWidth;
    uint hizHeight;
};

[[vk::push_constant]]
CellCullPush gCull;

[[vk::binding(7, 1)]]
RWStructuredBuffer<uint2> gCellsOut;
[[vk::binding(8, 1)]]
RWStructuredBuffer<uint> gCellArgsOut;
/// Cell entry index and packQuad bits
[[vk::binding(9, 1)]]
RWStructuredBuffer<uint2> gQuadsOut;

/// renderer::HiZPyramid
[[vk::binding(10, 1)]]
Sampler2D gHiz;

/// Mirrors renderer::VoxelPass::HizView
struct HizView
{
    /// Camera the pyramid was built from for the early test
    float4x4 viewProjection;
};

[[vk::binding(11, 1)]]
ConstantBuffer<HizView> gHizView;

/// Per global cell the count phase result as the cell entry then list | slot << kClassSlotShift or kNoBucket
[[vk::binding(12, 1)]]
RWStructuredBuffer<uint2> gCellClass;
static const uint kClassSlotShift = 5u;

static const uint kPhaseCount = 0u;
static const uint kPhaseAllocate = 1u;
static const uint kPhaseWrite = 2u;
static const uint kPhaseLate = 3u;

static const uint kNoBucket = 0xFFFFFFFFu;

/// Last instance whose first cell is at or before @p cell
uint instanceOfCell(uint cell)
{
    uint low = 0u;
    uint high = gCull.instanceCount;
    while (high - low > 1u)
    {
        const uint middle = (low + high) / 2u;
        if (gVoxelInstances[middle].cellBase <= cell)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

/// Largest axis scale so a world distance becomes voxels
float worldToVoxelScale(float4x4 worldToVoxel)
{
    return max(max(length(mul(worldToVoxel, float4(1.0, 0.0, 0.0, 0.0)).xyz), length(mul(worldToVoxel, float4(0.0, 1.0, 0.0, 0.0)).xyz)),
               length(mul(worldToVoxel, float4(0.0, 0.0, 1.0, 0.0)).xyz));
}

/// True when the box lies behind the farthest depth the pyramid holds over its screen rect
bool occludedByHiz(float4x4 voxelToClip, float3 boxMin, float3 boxMax)
{
    const float2 size = float2(float(gCull.hizWidth), float(gCull.hizHeight));
    float2 low = float2(3.4e38, 3.4e38);
    float2 high = -low;
    float nearest = kFarDepth;
    for (uint i = 0u; i < 8u; ++i)
    {
        const float4 clip = mul(voxelToClip, float4(lerp(boxMin, boxMax, kBoxCorners[i]), 1.0));
        // A corner on or behind the near plane has no screen rect to test
        if (clip.w <= 0.0 || clipNearDistance(clip) <= 0.0)
        {
            return false;
        }
        const float3 ndc = clip.xyz / clip.w;
        const float2 pixel = (ndc.xy * 0.5 + 0.5) * size;
        low = min(low, pixel);
        high = max(high, pixel);
        nearest = closerDepth(nearest, ndc.z);
    }
    // Off screen for the camera the pyramid saw so nothing there says it is hidden
    if (any(high < 0.0) || any(low >= size))
    {
        return false;
    }

    // One pixel of margin for the jitter the depth was drawn with
    const int2 last = int2(size) - 1;
    const int2 first = clamp(int2(floor(low)) - 1, int2(0, 0), last);
    const int2 limit = clamp(int2(floor(high)) + 1, int2(0, 0), last);

    // Level k texels are 2 << k pixels wide so the rect touches at most 2x2 of them
    const uint span = uint(max(limit.x - first.x, limit.y - first.y)) + 1u;
    const uint level = span <= 1u ? 0u : firstbithigh(span - 1u);
    // Only a pyramid capped at kMaxLevels lacks the level that covers the rect with 2x2 texels
    if (level >= gCull.hizLevels)
    {
        return false;
    }
    const int shift = int(level) + 1;
    const int2 a = first >> shift;
    const int2 b = limit >> shift;

    float farthest = gHiz.Load(int3(a, int(level))).x;
    farthest = fartherDepth(farthest, gHiz.Load(int3(b.x, a.y, int(level))).x);
    farthest = fartherDepth(farthest, gHiz.Load(int3(a.x, b.y, int(level))).x);
    farthest = fartherDepth(farthest, gHiz.Load(int3(b, int(level))).x);
    return isCloserDepth(farthest, nearest);
}

void cellBox(uint entry, uint3 cell, int3 dims, out float3 boxMin, out float3 boxMax)
{
    int3 low;
    int3 high;
    cellEntryBox(entry, cell, dims, low, high);
    boxMin = float3(low);
    boxMax = float3(high);
}

/// The camera or its near plane is inside so the box draws its back faces
bool insideBox(float3 camera, float guard, float3 boxMin, float3 boxMax)
{
    return all(camera >= boxMin - guard) && all(camera <= boxMax + guard);
}


float nearDistance(float3 camera, float3 boxMin, float3 boxMax)
{
    return length(camera - clamp(camera, boxMin, boxMax));
}

bool isNearCell(float3 camera, float3 boxMin, float3 boxMax)
{
    return nearDistance(camera, boxMin, boxMax) < gCull.nearVoxels;
}

uint3 coarseCellOf(VolumeRecord record, uint local)
{
    return uint3(local % record.coarseDimsX, (local / record.coarseDimsX) % record.coarseDimsY,
                 local / (record.coarseDimsX * record.coarseDimsY));
}

/// A near band or bucket 0 for the camera or the retest list or a distance bucket stepping by sqrt 2
uint classifyCell(uint globalCell, out uint entry)
{
    const uint instanceIndex = instanceOfCell(globalCell);
    entry = instanceIndex;

    const VoxelInstance instance = gVoxelInstances[instanceIndex];
    const VolumeRecord record = gVolumeRecords[instance.recordIndex];
    const uint local = globalCell - instance.cellBase;
    if (local >= record.coarseDimsX * record.coarseDimsY * record.coarseDimsZ)
    {
        return kNoBucket;
    }

    const uint3 cell = coarseCellOf(record, local);
    if (!coarseSolid(instance.recordIndex, int3(cell)))
    {
        return kNoBucket;
    }

    const int3 dims = int3(record.brickDimsX, record.brickDimsY, record.brickDimsZ) * int(kBrickDim);
    const float4x4 voxelToClip = mul(gCull.viewProjection, instance.voxelToWorld);
    const float3 cellMin = float3(int3(cell) * kCoarseVoxels);
    const float3 cellMax = float3(min(int3(cell) * kCoarseVoxels + kCoarseVoxels, dims));
    const float3 camera = mul(instance.worldToVoxel, float4(gCull.cameraNear.xyz, 1.0)).xyz;
    const float guard = gCull.cameraNear.w * worldToVoxelScale(instance.worldToVoxel);
    if (!insideBox(camera, guard, cellMin, cellMax) && outsideFrustum(voxelToClip, cellMin, cellMax))
    {
        return kNoBucket;
    }

    // Every pixel of the box marches so it shrinks to the occupied bricks
    uint3 low = uint3(3u, 3u, 3u);
    uint3 high = uint3(0u, 0u, 0u);
    bool occupied = false;
    const int3 firstBrick = int3(cell * kCoarseBricks);
    for (uint i = 0u; i < 64u; ++i)
    {
        const uint3 offset = uint3(i & 3u, (i >> 2) & 3u, i >> 4);
        if ((brickEntryAt(instance.recordIndex, firstBrick + int3(offset)) & 3u) != kTagEmpty)
        {
            low = min(low, offset);
            high = max(high, offset);
            occupied = true;
        }
    }
    if (!occupied)
    {
        return kNoBucket;
    }
    entry = packCellEntry(instanceIndex, low, high);

    float3 boxMin;
    float3 boxMax;
    cellBox(entry, cell, dims, boxMin, boxMax);
    const bool inside = insideBox(camera, guard, boxMin, boxMax);
    if (!inside && outsideFrustum(voxelToClip, boxMin, boxMax))
    {
        return kNoBucket;
    }

    if (!inside && gCull.hizLevels > 0u &&
        occludedByHiz(mul(gHizView.viewProjection, instance.previousVoxelToWorld), boxMin, boxMax))
    {
        return kRetestList;
    }
    const float near = nearDistance(camera, boxMin, boxMax);
    if (near < gCull.nearVoxels)
    {
        return kNearList + min(uint(near / gCull.nearVoxels * float(kNearBandCount)), kNearBandCount - 1u);
    }
    if (inside)
    {
        return 0u;
    }

    const float cells = length(camera - 0.5 * (boxMin + boxMax)) / float(kCoarseVoxels);
    return 1u + min(uint(2.0 * log2(max(cells, 1.0))), kCellBucketCount - 2u);
}

void allocateLists()
{
    // Read before the loop turns the counts into list ends
    uint nearCells = 0u;
    for (uint band = 0u; band < kNearBandCount; ++band)
    {
        nearCells += gCellArgsOut[kCellCursorsWord + kNearList + band];
    }

    const uint retestCells = gCellArgsOut[kCellCursorsWord + kRetestList];

    uint offset = 0u;
    for (uint list = 0u; list < kCellListCount; ++list)
    {
        // Filled by the late phase so it goes after everything else
        if (list == kLateList)
        {
            continue;
        }
        const uint count = gCellArgsOut[kCellCursorsWord + list];
        if (list < kCellDrawCount)
        {
            gCellArgsOut[list * 4u + 0u] = 36u;
            gCellArgsOut[list * 4u + 1u] = count;
            gCellArgsOut[list * 4u + 2u] = 0u;
            gCellArgsOut[list * 4u + 3u] = 0u;
        }
        gCellArgsOut[kCellOffsetsWord + list] = offset;
        offset += count;
        gCellArgsOut[kCellCursorsWord + list] = offset;
    }

    gCellArgsOut[kLateList * 4u + 0u] = 36u;
    gCellArgsOut[kLateList * 4u + 1u] = 0u;
    gCellArgsOut[kLateList * 4u + 2u] = 0u;
    gCellArgsOut[kLateList * 4u + 3u] = 0u;
    gCellArgsOut[kCellOffsetsWord + kLateList] = offset;
    gCellArgsOut[kCellCursorsWord + kLateList] = offset;
    gCellArgsOut[kCellOffsetsWord + kLateNearList] = offset + retestCells;
    gCellArgsOut[kCellCursorsWord + kLateNearList] = offset + retestCells;

    gCellArgsOut[kNearDispatchWord + 0u] = gCull.nearVoxels > 0.0 ? nearCells : 0u;
    gCellArgsOut[kNearDispatchWord + 1u] = 1u;
    gCellArgsOut[kNearDispatchWord + 2u] = 1u;

    gCellArgsOut[kRetestDispatchWord + 0u] = (retestCells + 63u) / 64u;
    gCellArgsOut[kRetestDispatchWord + 1u] = 1u;
    gCellArgsOut[kRetestDispatchWord + 2u] = 1u;

    gCellArgsOut[kLateMeshDispatchWord + 0u] = 0u;
    gCellArgsOut[kLateMeshDispatchWord + 1u] = 1u;
    gCellArgsOut[kLateMeshDispatchWord + 2u] = 1u;

    gCellArgsOut[kNearTaskWord + 0u] = gCull.nearVoxels > 0.0 ? nearCells : 0u;
    gCellArgsOut[kNearTaskWord + 1u] = kTaskGroupsPerCell;
    gCellArgsOut[kNearTaskWord + 2u] = 1u;
    gCellArgsOut[kLateTaskWord + 0u] = 0u;
    gCellArgsOut[kLateTaskWord + 1u] = kTaskGroupsPerCell;
    gCellArgsOut[kLateTaskWord + 2u] = 1u;

    // The mesh pass accumulates six vertices per quad into the vertex count
    gCellArgsOut[kQuadDrawWord + 0u] = 0u;
    gCellArgsOut[kQuadDrawWord + 1u] = 1u;
    gCellArgsOut[kQuadDrawWord + 2u] = 0u;
    gCellArgsOut[kQuadDrawWord + 3u] = 0u;

    gCellArgsOut[kOcclusionStatsWord + 0u] = retestCells;
    gCellArgsOut[kOcclusionStatsWord + 1u] = offset;

    gCellArgsOut[kLateQuadDrawWord + 0u] = 0u;
    gCellArgsOut[kLateQuadDrawWord + 1u] = 1u;
    gCellArgsOut[kLateQuadDrawWord + 2u] = 0u;
    gCellArgsOut[kLateQuadDrawWord + 3u] = 0u;
}

/// Moves a retest cell into the late list when the pyramid of this frame does not hide it
void retestCell(uint index)
{
    // The early mesh pass is done and the late one has not started so the quad count is where late quads begin
    if (index == 0u)
    {
        gCellArgsOut[kLateQuadDrawWord + 2u] = gCellArgsOut[kQuadDrawWord];
    }

    const uint first = gCellArgsOut[kCellOffsetsWord + kRetestList];
    if (index >= gCellArgsOut[kCellCursorsWord + kRetestList] - first)
    {
        return;
    }

    const uint2 entry = gCellsOut[first + index];
    const VoxelInstance instance = gVoxelInstances[cellEntryInstance(entry.x)];
    const VolumeRecord record = gVolumeRecords[instance.recordIndex];
    float3 boxMin;
    float3 boxMax;
    cellBox(entry.x, unpackCell(entry.y), int3(record.brickDimsX, record.brickDimsY, record.brickDimsZ) * int(kBrickDim), boxMin,
            boxMax);
    if (gCull.hizLevels > 0u && occludedByHiz(mul(gCull.viewProjection, instance.voxelToWorld), boxMin, boxMax))
    {
        return;
    }

    uint slot;
    const float3 camera = mul(instance.worldToVoxel, float4(gCull.cameraNear.xyz, 1.0)).xyz;
    if (isNearCell(camera, boxMin, boxMax))
    {
        InterlockedAdd(gCellArgsOut[kCellCursorsWord + kLateNearList], 0xFFFFFFFFu, slot);
        gCellsOut[slot - 1u] = entry;
        InterlockedAdd(gCellArgsOut[kLateMeshDispatchWord], 1u);
        InterlockedAdd(gCellArgsOut[kLateTaskWord], 1u);
        return;
    }

    InterlockedAdd(gCellArgsOut[kCellCursorsWord + kLateList], 1u, slot);
    gCellsOut[slot] = entry;
    InterlockedAdd(gCellArgsOut[kLateList * 4u + 1u], 1u);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    if (gCull.phase == kPhaseAllocate)
    {
        if (thread.x == 0u)
        {
            allocateLists();
        }
        return;
    }
    if (gCull.phase == kPhaseLate)
    {
        retestCell(thread.x);
        return;
    }

    if (thread.x >= gCull.totalCells)
    {
        return;
    }

    if (gCull.phase == kPhaseWrite)
    {
        const uint2 classified = gCellClass[thread.x];
        if (classified.y == kNoBucket)
        {
            return;
        }
        const uint list = classified.y & ((1u << kClassSlotShift) - 1u);
        const VoxelInstance instance = gVoxelInstances[cellEntryInstance(classified.x)];
        const uint3 cell = coarseCellOf(gVolumeRecords[instance.recordIndex], thread.x - instance.cellBase);
        gCellsOut[gCellArgsOut[kCellOffsetsWord + list] + (classified.y >> kClassSlotShift)] = uint2(classified.x, packCell(cell));
        return;
    }

    uint entry;
    const uint list = classifyCell(thread.x, entry);
    uint slot = 0u;
    if (list != kNoBucket)
    {
        InterlockedAdd(gCellArgsOut[kCellCursorsWord + list], 1u, slot);
    }
    gCellClass[thread.x] = uint2(entry, list == kNoBucket ? kNoBucket : list | (slot << kClassSlotShift));
}

/// Counts the quads when @p base is kNoBucket and writes them from @p base otherwise
uint meshBrick(uint volume, int3 brick, float3 camera, uint occupancy[16], uint entryIndex, uint brickInCell, uint base)
{
    const int3 brickMin = brick * int(kBrickDim);
    uint emitted = 0u;
    for (uint direction = 0u; direction < 6u; ++direction)
    {
        const uint axis = direction >> 1;
        const bool positive = (direction & 1u) == 0u;

        // Faces of one direction share a normal so a whole brick side facing away is skipped
        const float lowest = float(brickMin[axis]) + (positive ? 1.0 : 0.0);
        const float highest = float(brickMin[axis]) + (positive ? 8.0 : 7.0);
        if ((positive && camera[axis] <= lowest) || (!positive && camera[axis] >= highest))
        {
            continue;
        }

        int3 step = int3(0, 0, 0);
        step[axis] = positive ? 1 : -1;
        const uint neighbourEntry = brickEntryAt(volume, brick + step);
        uint neighbour[16];
        loadBrickSlices(neighbourEntry, neighbour);

        for (uint slice = 0u; slice < 8u; ++slice)
        {
            const float plane = float(brickMin[axis] + int(slice)) + (positive ? 1.0 : 0.0);
            if ((positive && camera[axis] <= plane) || (!positive && camera[axis] >= plane))
            {
                continue;
            }

            uint rows[8];
            exposedRows(occupancy, neighbour, axis, positive, slice, rows);

            for (uint v = 0u; v < 8u; ++v)
            {
                while (rows[v] != 0u)
                {
                    const uint u = firstbitlow(rows[v]);
                    const uint width = firstbitlow(~(rows[v] >> u));
                    const uint run = ((1u << width) - 1u) << u;
                    uint height = 1u;
                    while (v + height < 8u && (rows[v + height] & run) == run)
                    {
                        rows[v + height] &= ~run;
                        ++height;
                    }
                    rows[v] &= ~run;

                    if (base != kNoBucket && base + emitted < gCull.quadCapacity)
                    {
                        gQuadsOut[base + emitted] = uint2(entryIndex, packQuad(direction, slice, u, v, width, height, brickInCell));
                    }
                    ++emitted;
                }
            }
        }
    }
    return emitted;
}

/// One group per near cell and one thread per brick
[shader("compute")]
[numthreads(64, 1, 1)]
void meshMain(uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID)
{
    // The late near list fills downwards so its cursor ends on its first entry
    const uint entryIndex = (gCull.phase == kPhaseLate ? gCellArgsOut[kCellCursorsWord + kLateNearList]
                                                       : gCellArgsOut[kCellOffsetsWord + kNearList]) +
                            group.x;
    const uint2 entry = gCellsOut[entryIndex];
    const VoxelInstance instance = gVoxelInstances[cellEntryInstance(entry.x)];

    const uint brickInCell = local.x;
    const int3 brick = int3(unpackCell(entry.y) * kCoarseBricks + uint3(brickInCell & 3u, (brickInCell >> 2) & 3u, brickInCell >> 4));
    const uint brickEntry = brickEntryAt(instance.recordIndex, brick);
    if ((brickEntry & 3u) == kTagEmpty)
    {
        return;
    }

    // Only the late pyramid is final so the early pass cannot drop a brick for occlusion without retesting it
    const float4x4 voxelToClip = mul(gCull.viewProjection, instance.voxelToWorld);
    const float3 brickLow = float3(brick * int(kBrickDim));
    const float3 brickHigh = brickLow + float(kBrickDim);
    if (outsideFrustum(voxelToClip, brickLow, brickHigh))
    {
        return;
    }
    if (gCull.phase == kPhaseLate && gCull.hizLevels > 0u && occludedByHiz(voxelToClip, brickLow, brickHigh))
    {
        return;
    }

    uint occupancy[16];
    loadBrickSlices(brickEntry, occupancy);

    const float3 camera = mul(instance.worldToVoxel, float4(gCull.cameraNear.xyz, 1.0)).xyz;
    const uint count = meshBrick(instance.recordIndex, brick, camera, occupancy, entryIndex, brickInCell, kNoBucket);
    if (count == 0u)
    {
        return;
    }

    uint vertexBase;
    InterlockedAdd(gCellArgsOut[kQuadDrawWord], count * 6u, vertexBase);
    if (gCull.phase == kPhaseLate)
    {
        InterlockedAdd(gCellArgsOut[kLateQuadDrawWord], count * 6u);
    }
    meshBrick(instance.recordIndex, brick, camera, occupancy, entryIndex, brickInCell, vertexBase / 6u);
}
