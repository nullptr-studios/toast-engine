// Task stage of the voxel shadow mesh backend with one dispatch per volume and kTaskGroupsPerCell groups per coarse cell in x
import voxel_dda;
import voxel_mesh_common;
import voxel_task_payload;
import voxel_shadow_mesh_common;
import shadow_views;

/// Any view of the layer group since one task group feeds all of them
struct LightBrickView : IBrickView
{
    float4x4 voxelToWorld;

    bool sees(float3 low, float3 high)
    {
        for (uint view = 0u; view < gPush.viewCount; ++view)
        {
            if (!outsideFrustum(mul(gShadow.viewProjection[gPush.viewIndex + view], voxelToWorld), low, high))
            {
                return true;
            }
        }
        return false;
    }
}

/// The light in voxel space as a point with w 1 or the direction toward it with w 0
float4 lightEye(VoxelInstance instance)
{
    // The clip direction of growing depth is the eye of a perspective view and points away from an orthographic light
    const float4 away = mul(gShadow.inverseViewProjection[gPush.viewIndex], float4(0.0, 0.0, 1.0, 0.0));
    const float4 light = gPush.directional != 0u ? float4(-away.xyz, 0.0) : float4(away.xyz / away.w, 1.0);
    return mul(instance.worldToVoxel, light);
}

[shader("amplification")]
[numthreads(kTaskBricks, 1, 1)]
void shadowTaskMain(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
    const uint3 cell = uint3(group.x / kTaskGroupsPerCell, group.y, group.z);
    const uint brickInCell = (group.x % kTaskGroupsPerCell) * kTaskBricks + thread;
    const VoxelInstance instance = gVoxelInstances[gPush.instanceIndex];
    const int3 brick = int3(brickOfCell(cell, brickInCell));
    const LightBrickView view = { instance.voxelToWorld };

    buildTaskPayload(thread, brickInCell, packCell(cell), instance.recordIndex, brick, lightEye(instance), view);
    DispatchMesh((sPayload.quadCount + kMeshletQuads - 1u) / kMeshletQuads, 1u, 1u, sPayload);
}
