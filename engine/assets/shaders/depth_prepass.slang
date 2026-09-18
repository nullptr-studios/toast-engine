struct CameraUBO
{
    float4x4 view;
    float4x4 projection;
    float4x4 viewProjection;
};

[[vk::binding(0,0)]]
ConstantBuffer<CameraUBO> gCamera;

// Mirrors mesh.slang InstanceData and VulkanRenderer::InstanceData
struct InstanceData
{
    float4x4 model;
    uint jointOffset;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

[[vk::binding(13,0)]]
StructuredBuffer<InstanceData> gInstances;

struct PushConstants
{
    uint instanceBase;
};

[[vk::push_constant]]
PushConstants gPush;

struct VSInput
{
    [[vk::location(0)]]
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input, uint instanceID : SV_InstanceID)
{
    VSOutput output;
    float4x4 model = gInstances[gPush.instanceBase + instanceID].model;
    output.position = mul(gCamera.viewProjection, mul(model, float4(input.position, 1.0)));
    return output;
}
