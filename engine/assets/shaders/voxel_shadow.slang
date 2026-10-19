import voxel_dda;
import shadow_views;

// Mirrors ShadowPass k_caster_bias_slope since SV_Depth bypasses the pipeline depth bias
static const float kSlopeBias = 1.25;

/// World cap of the slope term so a grazing face never pushes its shadow further than this
static const float kMaxSlopeBiasMeters = 0.1;

/// Mirrors renderer::ShadowPass::VoxelShadowPush
struct PushConstants
{
    uint instanceIndex;
    uint viewIndex;

    /// Depth units of the rasterizer constant bias for this shadow format
    float constantBias;
    /// Nonzero for cascades whose rays march from behind the near plane
    uint directional;
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

/// The view ray of an NDC point from the near plane where t = 1 reaches the far plane
void viewRay(uint view, float2 ndc, out float3 nearWorld, out float3 worldDirection)
{
    const float4x4 inverseViewProjection = gShadow.inverseViewProjection[view];
    const float4 nearPoint = mul(inverseViewProjection, float4(ndc, 0.0, 1.0));
    const float4 farPoint = mul(inverseViewProjection, float4(ndc, 1.0, 1.0));
    nearWorld = nearPoint.xyz / nearPoint.w;
    worldDirection = farPoint.xyz / farPoint.w - nearWorld;
}

float shadowDepth(uint view, float3 world)
{
    const float4 clip = mul(gShadow.viewProjection[view], float4(world, 1.0));
    return clip.z / clip.w;
}

/// Depth of a neighbour ray where it crosses the plane of the hit face
float facePlaneDepth(uint view, float4x4 worldToVoxel, float2 ndc, uint axis, float plane)
{
    float3 nearWorld;
    float3 worldDirection;
    viewRay(view, ndc, nearWorld, worldDirection);
    const float3 origin = mul(worldToVoxel, float4(nearWorld, 1.0)).xyz;
    const float3 direction = mul(worldToVoxel, float4(worldDirection, 0.0)).xyz;
    const float t = abs(direction[axis]) > 1e-8 ? (plane - origin[axis]) / direction[axis] : 0.0;
    return shadowDepth(view, nearWorld + worldDirection * t);
}

/// False discards
bool casterDepth(VSOutput input, out float biasedDepth)
{
    const float4x4 worldToVoxel = gVoxelInstances[gPush.instanceIndex].worldToVoxel;
    const uint record = gVoxelInstances[gPush.instanceIndex].recordIndex;

    // Rays leave from the near plane
    const float2 ndc = input.clip.xy / input.clip.w;
    // NDC is smooth across the box unlike the hit depth
    const float2 ndcX = ndc + ddx(ndc);
    const float2 ndcY = ndc + ddy(ndc);

    float3 nearWorld;
    float3 worldDirection;
    viewRay(input.view, ndc, nearWorld, worldDirection);

    const float3 origin = mul(worldToVoxel, float4(nearWorld, 1.0)).xyz;
    const float3 direction = mul(worldToVoxel, float4(worldDirection, 0.0)).xyz;
    // A cascade clamps depth so a caster in front of its near plane still writes depth 0
    const float tMin = gPush.directional != 0u ? -kFloatMax : 0.0;
    const RayHit hit = marchRay(record, origin, direction, tMin, 1.0);

    biasedDepth = input.position.z;
    if (!hit.hit)
    {
        return false;
    }

    const float3 hitWorld = nearWorld + worldDirection * hit.t;
    const float depth = shadowDepth(input.view, hitWorld);

    // The receiver plane slope of the hit face as the rasterizer bias of a quad would see it
    float slopeBias = 0.0;
    if (hit.axis >= 0)
    {
        const uint axis = uint(hit.axis);
        const float plane = float(hit.voxel[axis]) + (direction[axis] > 0.0 ? 0.0 : 1.0);
        const float slope = max(abs(facePlaneDepth(input.view, worldToVoxel, ndcX, axis, plane) - depth),
                                abs(facePlaneDepth(input.view, worldToVoxel, ndcY, axis, plane) - depth));
        const float cap = shadowDepth(input.view, hitWorld + normalize(worldDirection) * kMaxSlopeBiasMeters) - depth;
        slopeBias = min(kSlopeBias * slope, cap);
    }
    biasedDepth = saturate(depth + slopeBias + gPush.constantBias);
    return true;
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
