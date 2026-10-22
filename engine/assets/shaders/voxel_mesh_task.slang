// Task stage of the near cell mesh shader backend
// One thread per brick counts the quads of its camera facing slices so every mesh group gets kMeshletQuads of them
import voxel;
import voxel_dda;
import voxel_mesh_common;
import voxel_task_payload;
import lighting;

/// gPush.bucket picks the list since the near and late near lists share these entry points
static const uint kMeshEarly = 0u;

struct CameraBrickView : IBrickView
{
    float4x4 voxelToClip;

    bool sees(float3 low, float3 high)
    {
        return !outsideFrustum(voxelToClip, low, high);
    }
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
    const int3 brick = int3(brickOfCell(unpackCell(cell.y), brickInCell));
    const float4 eye = mul(instance.worldToVoxel, float4(gCamera.position, 1.0));
    const CameraBrickView view = { mul(gCamera.viewProjection, instance.voxelToWorld) };

    buildTaskPayload(thread, brickInCell, entryIndex, instance.recordIndex, brick, eye, view);
    DispatchMesh((sPayload.quadCount + kMeshletQuads - 1u) / kMeshletQuads, 1u, 1u, sPayload);
}
