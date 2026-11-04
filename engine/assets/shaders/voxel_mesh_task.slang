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
    /// Only the late pyramid is final so the early list never drops a brick for occlusion
    bool testsPyramid;

    bool sees(float3 low, float3 high)
    {
        if (outsideFrustum(voxelToClip, low, high))
        {
            return false;
        }
        return !(testsPyramid && boxHiddenByHiz(voxelToClip, low, high));
    }
}

/// Brick of a cell for the n th thread so the bricks nearest the eye come first and early depth rejects the rest
/// The axis the eye is farthest from the cell along orders slowest and an axis flips when the eye sits on its high side
uint nearToFarBrick(uint linear, float3 eyeToCell)
{
    const float3 distance = abs(eyeToCell);
    const uint major = distance.x >= distance.y ? (distance.x >= distance.z ? 0u : 2u) : (distance.y >= distance.z ? 1u : 2u);
    const uint minor = distance.x < distance.y ? (distance.x < distance.z ? 0u : 2u) : (distance.y < distance.z ? 1u : 2u);
    const uint middle = 3u - major - minor;

    uint digits[3];
    digits[major] = (linear >> 4) & 3u;
    digits[middle] = (linear >> 2) & 3u;
    digits[minor] = linear & 3u;
    for (uint axis = 0u; axis < 3u; ++axis)
    {
        if (eyeToCell[axis] > 0.0)
        {
            digits[axis] = 3u - digits[axis];
        }
    }
    return digits[0] | (digits[1] << 2) | (digits[2] << 4);
}

[shader("amplification")]
[numthreads(kTaskBricks, 1, 1)]
void quadTaskMain(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
    // The late near list fills downwards so its cursor ends on its first entry
    const uint entryIndex = (gPush.bucket == kMeshEarly ? gVoxelCellArgs[kCellOffsetsWord + kNearList]
                                                         : gVoxelCellArgs[kCellCursorsWord + kLateNearList]) +
                            group.x;

    const uint2 cell = gVoxelCells[entryIndex];
    const VoxelInstance instance = gVoxelInstances[cellEntryInstance(cell.x)];
    const float4 eye = mul(instance.worldToVoxel, float4(gCamera.position, 1.0));
    const uint3 cellCoordinates = unpackCell(cell.y);
    const float3 cellCenter = float3(cellCoordinates * kCoarseBricks * kBrickDim) + 0.5 * float(kCoarseBricks * kBrickDim);
    const uint brickInCell = nearToFarBrick(group.y * kTaskBricks + thread, eye.xyz - cellCenter);
    const int3 brick = int3(brickOfCell(cellCoordinates, brickInCell));
    const CameraBrickView view = { mul(gCamera.viewProjection, instance.voxelToWorld), gPush.bucket != kMeshEarly && gPush.hizLevels > 0u };

    buildTaskPayload(thread, brickInCell, entryIndex, instance.recordIndex, brick, eye, view);
    DispatchMesh((sPayload.quadCount + kMeshletQuads - 1u) / kMeshletQuads, 1u, 1u, sPayload);
}
