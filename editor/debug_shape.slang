struct CameraUBO
{
    float4x4 view;
    float4x4 projection;
    float4x4 viewProjection;

    float3 cameraPosition;
    float time;
};

[[vk::binding(0,0)]]
ConstantBuffer<CameraUBO> gCamera;

struct PushConstants
{
    float4x4 model;
    float4 tint;
};

[[vk::push_constant]]
PushConstants gPush;

struct VSInput
{
    [[vk::location(0)]]
    float3 position : POSITION;

    [[vk::location(1)]]
    float4 color : COLOR;
};

struct VSOutput
{
    float4 position : SV_Position;

    [[vk::location(0)]]
    float4 color : COLOR;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    VSOutput output;

    float4 worldPos =
        mul(
            gPush.model,
            float4(input.position, 1.0));

    output.position =
        mul(
            gCamera.viewProjection,
            worldPos);

    output.color = input.color * gPush.tint;

    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    return input.color;
}
