struct CameraUBO
{
    float4x4 view;
    float4x4 projection;
    float4x4 viewProjection;
    
    float3 position;
    float time;
};

[[vk::binding(0,0)]]
ConstantBuffer<CameraUBO> gCamera;
[[vk::binding(0,1)]]
Texture2D<float4> gAlbedoTex;
[[vk::binding(1,1)]]
SamplerState gAlbedoSampler;

struct PushConstants
{
    float4x4 model;
};

[[vk::push_constant]]
PushConstants gPush;

struct VSInput
{
    [[vk::location(0)]]
    float3 position : POSITION;

    [[vk::location(1)]]
    float3 normal : NORMAL;

    [[vk::location(2)]]
    float2 uv : TEXCOORD0;

    [[vk::location(3)]]
    float4 tangent : TANGENT;
    
    [[vk::location(4)]]
    float3 color : COLOR;
};

struct VSOutput
{
    float4 position : SV_Position;

    [[vk::location(0)]]
    float3 worldNormal : NORMAL;

    [[vk::location(1)]]
    float2 uv : TEXCOORD0;
    
    [[vk::location(2)]]
    float3 color : COLOR;
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

    output.worldNormal =
        normalize(
            mul(
                (float3x3)gPush.model,
                input.normal));

    output.uv =
        input.uv;
        
    output.color = input.color;

    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    float3 albedo =
        gAlbedoTex.Sample(
            gAlbedoSampler,
            input.uv).rgb;

    float intensity =
        dot(input.worldNormal, float3(1.0, -1.0, 1.0)) * 0.5 + 0.5;
    float3 color =
        input.color * albedo * intensity;
        
    // float3 color =
    //     input.worldNormal * 0.5 + 0.5;

    return float4(1.0);
}