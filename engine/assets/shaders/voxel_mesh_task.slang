// Task stage of the near cell mesh shader backend
// kTaskGroupsPerCell groups per near cell and one thread per brick greedy count the camera facing slices of every brick so
// each mesh group gets a full window of kMeshletQuads quads across bricks
import voxel;
import voxel_dda;
import voxel_mesh_common;
import lighting;

groupshared QuadTaskPayload sPayload;
/// Each thread lists its slices here before the scan gives them their place in the payload
groupshared uint sStaged[kMaxEntries];
groupshared uint sEntryBase[kTaskBricks];
groupshared uint sQuadBase[kTaskBricks];

struct CountSink : IQuadSink
{
    [mutating]
    void quad(uint packedQuad) { }
}

[shader("amplification")]
[numthreads(kTaskBricks, 1, 1)]
void quadTaskMain(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
    const uint brickInCell = group.y * kTaskBricks + thread;
    // The late near list fills downwards so its cursor ends on its first entry
    const uint entryIndex = (gPush.bucket == kMeshEarly ? gVoxelCellArgs[kCellOffsetsWord + kNearList]
                                                         : gVoxelCellArgs[kCellCursorsWord + kLateNearList]) +
                            group.x;

    const uint2 cell = gVoxelCells[entryIndex];
    const VoxelInstance instance = gVoxelInstances[cellEntryInstance(cell.x)];
    const int3 brick = int3(brickOfCell(cell, brickInCell));
    const uint brickEntry = brickEntryAt(instance.recordIndex, brick);

    const int3 brickMin = brick * int(kBrickDim);
    const float3 brickLow = float3(brickMin);
    const bool visible = (brickEntry & 3u) != kTagEmpty &&
                         !outsideFrustum(mul(gCamera.viewProjection, instance.voxelToWorld), brickLow, brickLow + float(kBrickDim));

    const uint staged = thread * kBrickEntries;
    uint entries = 0u;
    uint quads = 0u;
    if (visible)
    {
        uint occupancy[16];
        for (uint word = 0u; word < 16u; ++word)
        {
            occupancy[word] = occupancyWord(brickEntry, word);
        }
        const float3 camera = mul(instance.worldToVoxel, float4(gCamera.position, 1.0)).xyz;

        CountSink sink;
        for (uint direction = 0u; direction < 6u; ++direction)
        {
            const uint axis = direction >> 1;
            const bool positive = (direction & 1u) == 0u;
            if (!sliceFacesCamera(camera, brickMin, axis, positive, positive ? 0u : 7u))
            {
                continue;
            }

            const uint neighbourEntry = brickEntryAt(instance.recordIndex, brick + directionStep(direction));
            uint neighbour[16];
            for (uint word = 0u; word < 16u; ++word)
            {
                neighbour[word] = occupancyWord(neighbourEntry, word);
            }

            uint faces[16];
            exposedFaces(occupancy, neighbour, direction, faces);
            for (uint slice = 0u; slice < 8u; ++slice)
            {
                if (!sliceFacesCamera(camera, brickMin, axis, positive, slice))
                {
                    continue;
                }
                uint rows[8];
                sliceRows(faces, axis, slice, rows);
                const uint count = greedySlice(rows, direction, slice, brickInCell, sink);
                if (count != 0u)
                {
                    sStaged[staged + entries] = (direction * 8u + slice) | (count << 6);
                    ++entries;
                    quads += count;
                }
            }
        }
    }
    sEntryBase[thread] = entries;
    sQuadBase[thread] = quads;
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
        sPayload.entryIndex = entryIndex;
        sPayload.entryCount = entryTotal;
        sPayload.quadCount = quadTotal;
    }
    GroupMemoryBarrierWithGroupSync();

    uint firstQuad = sQuadBase[thread];
    for (uint i = 0u; i < entries; ++i)
    {
        const uint slice = sStaged[staged + i];
        sPayload.entries[sEntryBase[thread] + i] = brickInCell | ((slice & 63u) << 6) | (firstQuad << 12);
        firstQuad += slice >> 6;
    }
    GroupMemoryBarrierWithGroupSync();

    DispatchMesh((sPayload.quadCount + kMeshletQuads - 1u) / kMeshletQuads, 1u, 1u, sPayload);
}
