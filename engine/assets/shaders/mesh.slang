import toast;

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

struct MaterialUBO
{
    [Reflect] [Color] [Name("Tint")]
    float4 tint;
};

[[vk::binding(0,1)]]
ConstantBuffer<MaterialUBO> gMaterial;

[Reflect] [Name("Albedo")]
[[vk::binding(1,1)]]
Sampler2D gAlbedo;

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

    float4 worldPos = mul(gPush.model, float4(input.position, 1.0));

    output.position = mul(gCamera.viewProjection, worldPos);
    output.worldNormal = normalize(mul((float3x3)gPush.model, input.normal));
    output.uv = input.uv;
    output.color = input.color;

    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    float4 albedo = gAlbedo.Sample(input.uv);

    return float4(albedo.rgb * gMaterial.tint.rgb, albedo.a * gMaterial.tint.a);
}
