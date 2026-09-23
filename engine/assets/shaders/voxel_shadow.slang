import voxel_dda;

// Must match renderer::shadows::k_max_shadow_views
static const uint kMaxShadowViews = 20;

// Mirror ShadowPass depth_bias_slope since SV_Depth bypasses the pipeline depth bias
static const float kSlopeBias = 3.0;

// Derivatives across a silhouette see the miss lanes so the slope term is capped
static const float kMaxSlopeBias = 0.005;

/// shadow_depth.slang declares only the first array of the same buffer
struct ShadowUBO
{
    float4x4 viewProjection[kMaxShadowViews];
    float4x4 inverseViewProjection[kMaxShadowViews];
};

[[vk::binding(0, 0)]]
ConstantBuffer<ShadowUBO> gShadow;

/// Mirrors renderer::ShadowPass::VoxelShadowPush
struct PushConstants
{
    uint instanceIndex;
    uint viewIndex;

    /// Depth units of the rasterizer constant bias for this shadow format
    float constantBias;
    uint pad0;
};

[[vk::push_constant]]
PushConstants gPush;

struct VSOutput
{
    float4 position : SV_Position;

    /// Perspective correct so clip.xy / clip.w is the exact NDC of the fragment
    [[vk::location(0)]]
    float4 clip : TEXCOORD0;

    [[vk::location(1)]]
    nointerpolation uint view : TEXCOORD1;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID, uint viewId : SV_ViewID)
{
    const VoxelInstance instance = gVoxelInstances[gPush.instanceIndex];
    const VolumeRecord record = gVolumeRecords[instance.recordIndex];
    const float3 voxels = float3(record.brickDimsX, record.brickDimsY, record.brickDimsZ) * float(kBrickDim);
    const float3 local = kBoxCorners[kBoxIndices[vertexId]] * voxels;

    VSOutput output;
    output.view = gPush.viewIndex + viewId;
    output.clip = mul(gShadow.viewProjection[output.view], mul(instance.voxelToWorld, float4(local, 1.0)));
    output.position = output.clip;
    return output;
}

/// False discards
bool casterDepth(VSOutput input, out float biasedDepth)
{
    const VoxelInstance instance = gVoxelInstances[gPush.instanceIndex];
    const float4x4 inverseViewProjection = gShadow.inverseViewProjection[input.view];

    // From the near plane so a box crossing it marches from where it is visible
    const float2 ndc = input.clip.xy / input.clip.w;
    const float4 nearPoint = mul(inverseViewProjection, float4(ndc, 0.0, 1.0));
    const float4 farPoint = mul(inverseViewProjection, float4(ndc, 1.0, 1.0));
    const float3 nearWorld = nearPoint.xyz / nearPoint.w;
    const float3 worldDirection = farPoint.xyz / farPoint.w - nearWorld;

    // t = 1 is the far plane
    const float3 origin = mul(instance.worldToVoxel, float4(nearWorld, 1.0)).xyz;
    const float3 direction = mul(instance.worldToVoxel, float4(worldDirection, 0.0)).xyz;
    const RayHit hit = marchRay(instance.recordIndex, origin, direction, 0.0, 1.0);

    float depth = input.position.z;
    if (hit.hit)
    {
        const float4 clip = mul(gShadow.viewProjection[input.view], float4(nearWorld + worldDirection * hit.t, 1.0));
        depth = clip.z / clip.w;
    }

    // Before the caller discards since derivatives need uniform control flow
    const float slope = max(abs(ddx(depth)), abs(ddy(depth)));
    biasedDepth = saturate(depth + min(kSlopeBias * slope, kMaxSlopeBias) + gPush.constantBias);
    return hit.hit;
}

// Front faces of a box wholly past the near plane so early depth rejects it behind earlier casters
[shader("fragment")]
void fragmentOutside(VSOutput input, out float depth : SV_DepthGreaterEqual)
{
    if (!casterDepth(input, depth))
    {
        discard;
    }

    // Slab entry can land a hair in front of the face
    depth = max(depth, input.position.z);
}

// Back faces with exact depth for a box crossing the near plane of any view in the group
[shader("fragment")]
void fragmentInside(VSOutput input, out float depth : SV_Depth)
{
    if (!casterDepth(input, depth))
    {
        discard;
    }
}
