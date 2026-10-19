// Stencil bit 1 covers every pixel of the outlined nodes and bit 2 the pixels where they are in front
import voxel_dda;
import depth_convention;

struct CameraUBO
{
    float4x4 view;
    float4x4 projection;
    float4x4 viewProjection;

    float3 position;
    float time;

    float4x4 jitteredViewProjection;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUBO> gCamera;

// Mirrors mesh.slang InstanceData and VulkanRenderer::InstanceData
struct InstanceData
{
    float4x4 model;
    float4x4 previousModel;
    uint jointOffset;
    uint flags;
    uint _pad0;
    uint _pad1;
};

[[vk::binding(13, 0)]]
StructuredBuffer<InstanceData> gInstances;

[[vk::binding(1, 0)]]
Sampler2D<float> gSceneDepth;

/// Mirrors renderer::SelectionOutline::MaskPush
struct PushConstants
{
    uint instance;
    uint testOcclusion;
};

[[vk::push_constant]]
PushConstants gPush;

/// Relative since voxel cells raster as quads in the scene and march here
static const float kDepthSlack = 0.002;

/// The mask shares the scene jitter so both depths sample the same point
bool unoccluded(int2 pixel, float depth)
{
    const float scene = gSceneDepth.Load(int3(pixel, 0));

#if TOAST_REVERSED_Z
    return scene <= depth * (1.0 + kDepthSlack);
#else
    return scene >= depth - kDepthSlack * (1.0 - depth);
#endif
}

struct MeshVSInput
{
    [[vk::location(0)]]
    float3 position : POSITION;
};

struct MeshVSOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
MeshVSOutput meshVertex(MeshVSInput input, uint instanceId : SV_InstanceID)
{
    const float4x4 model = gInstances[gPush.instance + instanceId].model;

    MeshVSOutput output;
    output.position = mul(gCamera.jitteredViewProjection, mul(model, float4(input.position, 1.0)));
    return output;
}

[shader("fragment")]
void meshFragment(MeshVSOutput input)
{
    if (gPush.testOcclusion != 0u && !unoccluded(int2(input.position.xy), input.position.z))
    {
        discard;
    }
}

struct VoxelVSOutput
{
    float4 position : SV_Position;

    [[vk::location(0)]]
    float3 worldPosition : TEXCOORD0;
};

// Back faces only so each pixel marches once even with the camera inside the volume
[shader("vertex")]
VoxelVSOutput voxelVertex(uint vertexId : SV_VertexID)
{
    const VoxelInstance instance = gVoxelInstances[gPush.instance];
    const VolumeRecord record = gVolumeRecords[instance.recordIndex];
    const float3 voxels = float3(record.brickDimsX, record.brickDimsY, record.brickDimsZ) * float(kBrickDim);

    // A mirrored transform flips winding so swap two corners of each triangle to keep one cull mode
    uint corner = vertexId % 3u;
    if ((instance.flags & kInstanceMirrored) != 0u && corner != 0u)
    {
        corner = 3u - corner;
    }
    const float3 local = kBoxCorners[kBoxIndices[vertexId - (vertexId % 3u) + corner]] * voxels;
    const float4 world = mul(instance.voxelToWorld, float4(local, 1.0));

    VoxelVSOutput output;
    output.worldPosition = world.xyz;
    output.position = mul(gCamera.jitteredViewProjection, world);
    return output;
}

[shader("fragment")]
void voxelFragment(VoxelVSOutput input)
{
    const VoxelInstance instance = gVoxelInstances[gPush.instance];
    const float3 camera = gCamera.position;

    // t = 1 is the back face
    const float3 worldDirection = input.worldPosition - camera;
    const float3 origin = mul(instance.worldToVoxel, float4(camera, 1.0)).xyz;
    const float3 direction = mul(instance.worldToVoxel, float4(worldDirection, 0.0)).xyz;

    const float nearOrigin = clipNearDistance(mul(gCamera.viewProjection, float4(camera, 1.0)));
    const float nearStep = clipNearDistance(mul(gCamera.viewProjection, float4(worldDirection, 0.0)));
    const float tMin = nearStep > 0.0 ? max(0.0, -nearOrigin / nearStep) : 0.0;

    const RayHit hit = marchRay(instance.recordIndex, origin, direction, tMin, 1.0, false);
    if (!hit.hit)
    {
        discard;
    }
    if (gPush.testOcclusion != 0u)
    {
        const float4 clip = mul(gCamera.jitteredViewProjection, float4(camera + worldDirection * hit.t, 1.0));
        if (!unoccluded(int2(input.position.xy), clip.z / clip.w))
        {
            discard;
        }
    }
}
