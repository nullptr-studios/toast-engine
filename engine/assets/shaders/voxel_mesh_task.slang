// Task stage of the near cell mesh shader backend
// kTaskGroupsPerCell groups per near cell and one thread per brick pack the visible exposed slices of every side of a brick
// into meshlets of at most 64 exposed faces
import voxel;
import voxel_dda;
import voxel_mesh_common;
import lighting;

groupshared QuadTaskPayload sPayload;
groupshared uint sMeshletCount;

void appendMeshlet(uint brickInCell, uint2 mask)
{
    uint index;
    InterlockedAdd(sMeshletCount, 1u, index);
    sPayload.meshlets[index] = uint2(brickInCell | (mask.y << 6), mask.x);
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
    if (thread == 0u)
    {
        sMeshletCount = 0u;
        sPayload.entryIndex = entryIndex;
    }
    GroupMemoryBarrierWithGroupSync();

    const uint2 cell = gVoxelCells[entryIndex];
    const VoxelInstance instance = gVoxelInstances[cellEntryInstance(cell.x)];
    const int3 brick = int3(brickOfCell(cell, brickInCell));
    const uint brickEntry = brickEntryAt(instance.recordIndex, brick);

    const int3 brickMin = brick * int(kBrickDim);
    const float3 brickLow = float3(brickMin);
    const bool visible = (brickEntry & 3u) != kTagEmpty &&
                         !outsideFrustum(mul(gCamera.viewProjection, instance.voxelToWorld), brickLow, brickLow + float(kBrickDim));
    if (visible)
    {
        uint occupancy[16];
        for (uint word = 0u; word < 16u; ++word)
        {
            occupancy[word] = occupancyWord(brickEntry, word);
        }
        const float3 camera = mul(instance.worldToVoxel, float4(gCamera.position, 1.0)).xyz;

        // Greedy quads never outnumber exposed faces so slices of every side pack while their faces fit one meshlet
        uint2 mask = uint2(0u, 0u);
        uint faces = 0u;
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

            uint sideFaces[16];
            exposedFaces(occupancy, neighbour, direction, sideFaces);
            for (uint slice = 0u; slice < 8u; ++slice)
            {
                if (!sliceFacesCamera(camera, brickMin, axis, positive, slice))
                {
                    continue;
                }
                const uint exposed = sliceFaceCount(sideFaces, axis, slice);
                if (exposed == 0u)
                {
                    continue;
                }

                if (faces + exposed > kMeshletQuads)
                {
                    appendMeshlet(brickInCell, mask);
                    mask = uint2(0u, 0u);
                    faces = 0u;
                }
                const uint bit = direction * 8u + slice;
                if (bit < 32u)
                {
                    mask.x |= 1u << bit;
                }
                else
                {
                    mask.y |= 1u << (bit - 32u);
                }
                faces += exposed;
            }
        }
        if (faces > 0u)
        {
            appendMeshlet(brickInCell, mask);
        }
    }

    GroupMemoryBarrierWithGroupSync();
    DispatchMesh(sMeshletCount, 1u, 1u, sPayload);
}
