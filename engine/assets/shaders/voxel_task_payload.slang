// Payload building shared by the camera and shadow task stages and never imported by a mesh stage
import voxel_dda;
import voxel_mesh_common;

groupshared QuadTaskPayload sPayload;
/// Each thread lists its slices here before the scan gives them their place in the payload
groupshared uint sStaged[kMaxEntries];
groupshared uint sEntryBase[kTaskBricks];
groupshared uint sQuadBase[kTaskBricks];

/// Brick culling of the task stage
interface IBrickView
{
    bool sees(float3 low, float3 high);
}

struct StagingSink : ISliceSink
{
    uint staged;
    uint entries;
    uint quads;

    [mutating]
    void slice(uint directionSlice, uint count)
    {
        sStaged[staged + entries] = directionSlice | (count << 6);
        ++entries;
        quads += count;
    }
}

/// One thread per brick fills sPayload so every mesh group gets kMeshletQuads quads and the entry point dispatches it
void buildTaskPayload<View : IBrickView>(uint thread, uint brickInCell, uint cell, uint recordIndex, int3 brick, float4 eye, View view)
{
    const uint brickEntry = brickEntryAt(recordIndex, brick);
    const float3 brickLow = float3(brick * int(kBrickDim));

    StagingSink sink = { thread * kBrickEntries, 0u, 0u };
    if ((brickEntry & 3u) != kTagEmpty && view.sees(brickLow, brickLow + float(kBrickDim)))
    {
        countBrickSlices(recordIndex, brick, brickEntry, brickInCell, eye, sink);
    }
    sEntryBase[thread] = sink.entries;
    sQuadBase[thread] = sink.quads;
    GroupMemoryBarrierWithGroupSync();

    if (thread == 0u)
    {
        uint entryTotal = 0u;
        uint quadTotal = 0u;
        for (uint i = 0u; i < kTaskBricks; ++i)
        {
            const uint threadEntries = sEntryBase[i];
            const uint threadQuads = sQuadBase[i];
            sEntryBase[i] = entryTotal;
            sQuadBase[i] = quadTotal;
            entryTotal += threadEntries;
            quadTotal += threadQuads;
        }
        sPayload.cell = cell;
        sPayload.entryCount = entryTotal;
        sPayload.quadCount = quadTotal;
    }
    GroupMemoryBarrierWithGroupSync();

    uint firstQuad = sQuadBase[thread];
    for (uint i = 0u; i < sink.entries; ++i)
    {
        const uint slice = sStaged[sink.staged + i];
        sPayload.entries[sEntryBase[thread] + i] = brickInCell | ((slice & 63u) << 6) | (firstQuad << 12);
        firstQuad += slice >> 6;
    }
    GroupMemoryBarrierWithGroupSync();
}
