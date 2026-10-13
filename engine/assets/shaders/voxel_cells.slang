//   count     each surviving cell adds one to its list
//   allocate  one thread turns the counts into offsets and indirect commands
//   write     each surviving cell takes a slot in its list
//   mesh      one group per near cell turns the exposed faces of its bricks into greedy quads
import voxel_dda;

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
    uint pad0;
    uint pad1;
    uint pad2;
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

static const uint kPhaseCount = 0u;
static const uint kPhaseAllocate = 1u;
static const uint kPhaseWrite = 2u;

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

bool outsideFrustum(float4x4 voxelToClip, float3 boxMin, float3 boxMax)
{
    uint outside = 0x1Fu;
    for (uint i = 0u; i < 8u; ++i)
    {
        const float3 corner = lerp(boxMin, boxMax, kBoxCorners[i]);
        const float4 clip = mul(voxelToClip, float4(corner, 1.0));
        uint flags = 0u;
        flags |= clip.x < -clip.w ? 1u : 0u;
        flags |= clip.x > clip.w ? 2u : 0u;
        flags |= clip.y < -clip.w ? 4u : 0u;
        flags |= clip.y > clip.w ? 8u : 0u;
        flags |= clip.w <= 0.0 ? 16u : 0u;
        outside &= flags;
    }
    return outside != 0u;
}

/// Near cells go to kNearList and camera cells to bucket 0 while the rest step by sqrt 2 in distance
uint classifyCell(uint globalCell, out uint instanceIndex, out uint packed)
{
    instanceIndex = instanceOfCell(globalCell);
    packed = 0u;

    const VoxelInstance instance = gVoxelInstances[instanceIndex];
    const VolumeRecord record = gVolumeRecords[instance.recordIndex];
    const uint local = globalCell - instance.cellBase;
    const uint3 coarseDims = uint3(record.coarseDimsX, record.coarseDimsY, record.coarseDimsZ);
    if (local >= coarseDims.x * coarseDims.y * coarseDims.z)
    {
        return kNoBucket;
    }

    const uint3 cell = uint3(local % coarseDims.x, (local / coarseDims.x) % coarseDims.y, local / (coarseDims.x * coarseDims.y));
    if (!coarseSolid(instance.recordIndex, int3(cell)))
    {
        return kNoBucket;
    }
    packed = packCell(cell);

    const int3 dims = int3(record.brickDimsX, record.brickDimsY, record.brickDimsZ) * int(kBrickDim);
    const float3 boxMin = float3(int3(cell) * kCoarseVoxels);
    const float3 boxMax = float3(min(int3(cell) * kCoarseVoxels + kCoarseVoxels, dims));

    const float3 camera = mul(instance.worldToVoxel, float4(gCull.cameraNear.xyz, 1.0)).xyz;
    const float guard = gCull.cameraNear.w * worldToVoxelScale(instance.worldToVoxel);
    const bool inside = all(camera >= boxMin - guard) && all(camera <= boxMax + guard);
    if (!inside && outsideFrustum(mul(gCull.viewProjection, instance.voxelToWorld), boxMin, boxMax))
    {
        return kNoBucket;
    }

    if (length(camera - clamp(camera, boxMin, boxMax)) < gCull.nearVoxels)
    {
        return kNearList;
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
    // Read before the loop turns the cursors into offsets
    const uint nearCells = gCellArgsOut[kCellCursorsWord + kNearList];

    uint offset = 0u;
    for (uint list = 0u; list < kCellListCount; ++list)
    {
        const uint count = gCellArgsOut[kCellCursorsWord + list];
        if (list < kCellBucketCount)
        {
            gCellArgsOut[list * 4u + 0u] = 36u;
            gCellArgsOut[list * 4u + 1u] = count;
            gCellArgsOut[list * 4u + 2u] = 0u;
            gCellArgsOut[list * 4u + 3u] = 0u;
        }
        gCellArgsOut[kCellOffsetsWord + list] = offset;
        gCellArgsOut[kCellCursorsWord + list] = offset;
        offset += count;
    }

    gCellArgsOut[kNearDispatchWord + 0u] = gCull.nearVoxels > 0.0 ? nearCells : 0u;
    gCellArgsOut[kNearDispatchWord + 1u] = 1u;
    gCellArgsOut[kNearDispatchWord + 2u] = 1u;

    // The mesh pass accumulates six vertices per quad into the vertex count
    gCellArgsOut[kQuadDrawWord + 0u] = 0u;
    gCellArgsOut[kQuadDrawWord + 1u] = 1u;
    gCellArgsOut[kQuadDrawWord + 2u] = 0u;
    gCellArgsOut[kQuadDrawWord + 3u] = 0u;
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

    if (thread.x >= gCull.totalCells)
    {
        return;
    }

    uint instanceIndex;
    uint packed;
    const uint list = classifyCell(thread.x, instanceIndex, packed);
    if (list == kNoBucket)
    {
        return;
    }

    uint slot;
    InterlockedAdd(gCellArgsOut[kCellCursorsWord + list], 1u, slot);
    if (gCull.phase == kPhaseWrite)
    {
        gCellsOut[slot] = uint2(instanceIndex, packed);
    }
}

/// Grid entry with the empty tag outside the volume
uint brickEntryAt(uint volume, int3 brick)
{
    const int3 dims = int3(gVolumeRecords[volume].brickDimsX, gVolumeRecords[volume].brickDimsY, gVolumeRecords[volume].brickDimsZ);
    if (any(brick < 0) || any(brick >= dims))
    {
        return kTagEmpty;
    }
    return gridEntry(volume, uint3(brick));
}

/// Occupancy word in the pooled layout so uniform bricks read as solid
uint occupancyWord(uint entry, uint word)
{
    const uint tag = entry & 3u;
    if (tag == kTagEmpty)
    {
        return 0u;
    }
    if (tag == kTagUniform)
    {
        return 0xFFFFFFFFu;
    }
    return gBrickOccupancy[((entry >> 2) << 4) + word];
}

/// Eight x bits of row y in slice z
uint occupancyRow(uint occupancy[16], uint z, uint y)
{
    return (occupancy[(z << 1) + (y >> 2)] >> ((y & 3u) << 3)) & 0xFFu;
}

/// Exposed faces of one slice as rows over v with bits over u
void exposedRows(uint occupancy[16], uint neighbour[16], uint axis, bool positive, uint slice, out uint rows[8])
{
    for (uint v = 0u; v < 8u; ++v)
    {
        uint mask = 0u;
        if (axis == 2u)
        {
            const uint current = occupancyRow(occupancy, slice, v);
            uint other;
            if (positive)
            {
                other = slice < 7u ? occupancyRow(occupancy, slice + 1u, v) : occupancyRow(neighbour, 0u, v);
            }
            else
            {
                other = slice > 0u ? occupancyRow(occupancy, slice - 1u, v) : occupancyRow(neighbour, 7u, v);
            }
            mask = current & ~other;
        }
        else if (axis == 1u)
        {
            const uint current = occupancyRow(occupancy, v, slice);
            uint other;
            if (positive)
            {
                other = slice < 7u ? occupancyRow(occupancy, v, slice + 1u) : occupancyRow(neighbour, v, 0u);
            }
            else
            {
                other = slice > 0u ? occupancyRow(occupancy, v, slice - 1u) : occupancyRow(neighbour, v, 7u);
            }
            mask = current & ~other;
        }
        else
        {
            for (uint y = 0u; y < 8u; ++y)
            {
                const uint row = occupancyRow(occupancy, v, y);
                uint other;
                if (positive)
                {
                    other = slice < 7u ? row >> (slice + 1u) : occupancyRow(neighbour, v, y);
                }
                else
                {
                    other = slice > 0u ? row >> (slice - 1u) : occupancyRow(neighbour, v, y) >> 7u;
                }
                mask |= (((row >> slice) & ~other) & 1u) << y;
            }
        }
        rows[v] = mask & 0xFFu;
    }
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
        for (uint word = 0u; word < 16u; ++word)
        {
            neighbour[word] = occupancyWord(neighbourEntry, word);
        }

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
    const uint entryIndex = gCellArgsOut[kCellOffsetsWord + kNearList] + group.x;
    const uint2 entry = gCellsOut[entryIndex];
    const VoxelInstance instance = gVoxelInstances[entry.x];

    const uint brickInCell = local.x;
    const int3 brick = int3(unpackCell(entry.y) * kCoarseBricks + uint3(brickInCell & 3u, (brickInCell >> 2) & 3u, brickInCell >> 4));
    const uint brickEntry = brickEntryAt(instance.recordIndex, brick);
    if ((brickEntry & 3u) == kTagEmpty)
    {
        return;
    }

    uint occupancy[16];
    for (uint word = 0u; word < 16u; ++word)
    {
        occupancy[word] = occupancyWord(brickEntry, word);
    }

    const float3 camera = mul(instance.worldToVoxel, float4(gCull.cameraNear.xyz, 1.0)).xyz;
    const uint count = meshBrick(instance.recordIndex, brick, camera, occupancy, entryIndex, brickInCell, kNoBucket);
    if (count == 0u)
    {
        return;
    }

    uint vertexBase;
    InterlockedAdd(gCellArgsOut[kQuadDrawWord], count * 6u, vertexBase);
    meshBrick(instance.recordIndex, brick, camera, occupancy, entryIndex, brickInCell, vertexBase / 6u);
}
