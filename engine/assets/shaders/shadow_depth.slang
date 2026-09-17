// Must match renderer::shadows::k_max_shadow_views
static const uint kMaxShadowViews = 20;

struct ShadowUBO
{
    float4x4 viewProjection[kMaxShadowViews];
};

[[vk::binding(0,0)]]
ConstantBuffer<ShadowUBO> gShadow;

// Mirrors VulkanRenderer::InstanceData
struct InstanceData
{
    float4x4 model;
    uint jointOffset;
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::binding(2,0)]]
StructuredBuffer<InstanceData> gInstances;

struct PushConstants
{
    uint instanceBase;
    uint viewIndex;
};

[[vk::push_constant]]
PushConstants gPush;

// The bound buffer is the full renderer::Vertex stream
struct VSInput
{
    [[vk::location(0)]]
    float3 position : POSITION;
};

[shader("vertex")]
float4 vertexMain(VSInput input, uint instanceID : SV_InstanceID, uint viewID : SV_ViewID) : SV_Position
{
    InstanceData instance = gInstances[gPush.instanceBase + instanceID];
    return mul(gShadow.viewProjection[gPush.viewIndex + viewID], mul(instance.model, float4(input.position, 1.0)));
}
