// Compacts the non-empty coarse cells of every drawn voxel volume into distance buckets for renderer::VoxelPass
// Three dispatches per frame over one thread per coarse cell
//   count     each surviving cell adds one to its bucket
//   allocate  one thread turns the counts into offsets and indirect draw commands
//   write     each surviving cell takes a slot in its bucket
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
    uint pad0;
};

[[vk::push_constant]]
CellCullPush gCull;

[[vk::binding(7, 1)]]
RWStructuredBuffer<uint2> gCellsOut;
[[vk::binding(8, 1)]]
RWStructuredBuffer<uint> gCellArgsOut;

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

/// Bucket 0 holds cells the camera or near plane is inside and the rest step by a factor of sqrt 2 in distance
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
    if (all(camera >= boxMin - guard) && all(camera <= boxMax + guard))
    {
        return 0u;
    }

    if (outsideFrustum(mul(gCull.viewProjection, instance.voxelToWorld), boxMin, boxMax))
    {
        return kNoBucket;
    }

    const float cells = length(camera - 0.5 * (boxMin + boxMax)) / float(kCoarseVoxels);
    return 1u + min(uint(2.0 * log2(max(cells, 1.0))), kCellBucketCount - 2u);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    if (gCull.phase == kPhaseAllocate)
    {
        if (thread.x != 0u)
        {
            return;
        }
        uint offset = 0u;
        for (uint bucket = 0u; bucket < kCellBucketCount; ++bucket)
        {
            const uint count = gCellArgsOut[kCellCursorsWord + bucket];
            gCellArgsOut[bucket * 4u + 0u] = 36u;
            gCellArgsOut[bucket * 4u + 1u] = count;
            gCellArgsOut[bucket * 4u + 2u] = 0u;
            gCellArgsOut[bucket * 4u + 3u] = 0u;
            gCellArgsOut[kCellOffsetsWord + bucket] = offset;
            gCellArgsOut[kCellCursorsWord + bucket] = offset;
            offset += count;
        }
        return;
    }

    if (thread.x >= gCull.totalCells)
    {
        return;
    }

    uint instanceIndex;
    uint packed;
    const uint bucket = classifyCell(thread.x, instanceIndex, packed);
    if (bucket == kNoBucket)
    {
        return;
    }

    uint slot;
    InterlockedAdd(gCellArgsOut[kCellCursorsWord + bucket], 1u, slot);
    if (gCull.phase == kPhaseWrite)
    {
        gCellsOut[slot] = uint2(instanceIndex, packed);
    }
}
