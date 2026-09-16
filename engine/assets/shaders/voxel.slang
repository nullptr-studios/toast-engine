import voxel_dda;

struct CameraUBO
{
    float4x4 view;
    float4x4 projection;
    float4x4 viewProjection;

    float3 cameraPosition;
    float time;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUBO> gCamera;

struct PushConstants
{
    uint instanceIndex;
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::push_constant]]
PushConstants gPush;

// Counter clockwise from outside
static const float3 kBoxCorners[8] = {
    float3(0.0, 0.0, 0.0), float3(1.0, 0.0, 0.0), float3(1.0, 1.0, 0.0), float3(0.0, 1.0, 0.0),
    float3(0.0, 0.0, 1.0), float3(1.0, 0.0, 1.0), float3(1.0, 1.0, 1.0), float3(0.0, 1.0, 1.0),
};

static const uint kBoxIndices[36] = {
    0, 2, 1, 0, 3, 2,    // -Z
    4, 5, 6, 4, 6, 7,    // +Z
    0, 1, 5, 0, 5, 4,    // -Y
    3, 7, 6, 3, 6, 2,    // +Y
    0, 4, 7, 0, 7, 3,    // -X
    1, 2, 6, 1, 6, 5,    // +X
};

struct VSOutput
{
    float4 position : SV_Position;

    [[vk::location(0)]]
    float3 worldPosition : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    const VoxelInstance instance = gVoxelInstances[gPush.instanceIndex];
    const VolumeRecord record = gVolumeRecords[instance.recordIndex];
    const float3 voxels = float3(record.brickDimsX, record.brickDimsY, record.brickDimsZ) * 8.0;

    const float3 local = kBoxCorners[kBoxIndices[vertexId]] * voxels;
    const float4 world = mul(instance.voxelToWorld, float4(local, 1.0));

    VSOutput output;
    output.worldPosition = world.xyz;
    output.position = mul(gCamera.viewProjection, world);
    return output;
}

struct FSOutput
{
    float4 color : SV_Target0;

    /// xyz world normal w roughness
    float4 normalRoughness : SV_Target1;

    float4 indirectDiffuse : SV_Target2;
};

bool shade(VSOutput input, out FSOutput output, out float depth)
{
    output.color = float4(0.0, 0.0, 0.0, 1.0);
    output.normalRoughness = float4(0.0, 0.0, 1.0, 1.0);
    output.indirectDiffuse = float4(0.0, 0.0, 0.0, 0.0);
    depth = input.position.z;

    const VoxelInstance instance = gVoxelInstances[gPush.instanceIndex];
    const float3 camera = gCamera.cameraPosition;

    // t = 1 is the box surface
    const float3 worldDirection = input.worldPosition - camera;
    const float3 origin = mul(instance.worldToVoxel, float4(camera, 1.0)).xyz;
    const float3 direction = mul(instance.worldToVoxel, float4(worldDirection, 0.0)).xyz;

    // Vulkan clips at z = 0 so never hit in front of the near plane
    const float clipOriginZ = mul(gCamera.viewProjection, float4(camera, 1.0)).z;
    const float clipStepZ = mul(gCamera.viewProjection, float4(worldDirection, 0.0)).z;
    const float tMin = clipStepZ > 0.0 ? max(0.0, -clipOriginZ / clipStepZ) : 0.0;

    const RayHit hit = marchRay(instance.recordIndex, origin, direction, tMin, kFloatMax);
    if (!hit.hit)
    {
        return false;
    }

    const float3 hitWorld = camera + worldDirection * hit.t;
    const float4 clip = mul(gCamera.viewProjection, float4(hitWorld, 1.0));
    depth = clip.z / clip.w;

    float3 normal = -normalize(worldDirection);
    if (hit.axis >= 0)
    {
        float3 voxelNormal = float3(0.0, 0.0, 0.0);
        voxelNormal[hit.axis] = direction[hit.axis] > 0.0 ? -1.0 : 1.0;
        normal = normalize(mul(transpose((float3x3)instance.worldToVoxel), voxelNormal));
    }

    const VolumeRecord record = gVolumeRecords[instance.recordIndex];
    const PaletteSample palette = samplePalette(record, hit.material);

    const float3 toCamera = normalize(camera - hitWorld);
    const float facing = saturate(dot(normal, toCamera));

    output.color = float4(palette.albedo * (0.2 + 0.8 * facing) + palette.albedo * palette.emissive, 1.0);
    output.normalRoughness = float4(normal, palette.roughness);
    return true;
}

[shader("fragment")]
FSOutput fragmentOutside(VSOutput input, out float depth : SV_DepthGreaterEqual)
{
    FSOutput output;
    if (!shade(input, output, depth))
    {
        discard;
    }

    // Slab entry can land a hair in front of the face
    depth = max(depth, input.position.z);
    return output;
}

[shader("fragment")]
FSOutput fragmentInside(VSOutput input, out float depth : SV_Depth)
{
    FSOutput output;
    if (!shade(input, output, depth))
    {
        discard;
    }
    return output;
}
