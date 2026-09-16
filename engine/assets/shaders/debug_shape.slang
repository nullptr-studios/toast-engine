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

// Locations are renderer::Vertex attribute indices
struct MeshVSInput
{
    [[vk::location(0)]]
    float3 position : POSITION;

    [[vk::location(1)]]
    float3 normal : NORMAL;

    [[vk::location(4)]]
    float3 color : COLOR;
};

[shader("vertex")]
VSOutput vertexMesh(MeshVSInput input)
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

    float3 worldNormal =
        normalize(
            mul(
                (float3x3)gPush.model,
                input.normal));

    float lambert = saturate(dot(worldNormal, normalize(float3(0.35, 0.5, 0.8))));
    float shade = lerp(0.45, 1.0, lambert);

    output.color = float4(input.color * gPush.tint.rgb * shade, gPush.tint.a);

    return output;
}

[shader("fragment")]
float4 fragmentMesh(VSOutput input) : SV_Target0
{
    return input.color;
}
