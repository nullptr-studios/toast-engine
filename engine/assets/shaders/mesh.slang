import toast;

import lighting;

struct MaterialUBO
{
    [Reflect] [Color] [Name("Tint")]
    float4 tint;

    [Reflect] [Name("Metallic")] [Range(0.0, 1.0)]
    float metallic;

    [Reflect] [Name("Roughness")] [Range(0.045, 1.0)]
    float roughness;

    [Reflect] [Name("Alpha Cutoff")] [Range(0.0, 1.0)]
    float alphaCutoff;

    [Reflect] [Name("Occlusion Strength")] [Range(0.0, 1.0)]
    float occlusionStrength;

    [Reflect] [Color] [Name("Emissive Color")]
    float3 emissiveColor;

    [Reflect] [Name("Emissive Intensity")] [Range(0.0, 64.0)]
    float emissiveIntensity;

    // 0 R 1 G 2 B 3 A. No [Group] since grouped parameters live in the group TOML table
    [Reflect] [Name("Metallic Channel")] [Range(0.0, 3.0)]
    float metallicChannel;

    [Reflect] [Name("Roughness Channel")] [Range(0.0, 3.0)]
    float roughnessChannel;

    [Reflect] [Name("Occlusion Channel")] [Range(0.0, 3.0)]
    float occlusionChannel;
};

[[vk::binding(0,1)]]
ConstantBuffer<MaterialUBO> gMaterial;

[Reflect] [Name("Albedo")]
[[vk::binding(1,1)]]
Sampler2D gAlbedo;

[Reflect] [Linear] [Name("Normal")] [Default("flat_normal")]
[[vk::binding(2,1)]]
Sampler2D gNormal;

[Reflect] [Linear] [Name("Metallic Map")]
[[vk::binding(3,1)]]
Sampler2D gMetallicMap;

[Reflect] [Linear] [Name("Roughness Map")]
[[vk::binding(4,1)]]
Sampler2D gRoughnessMap;

[Reflect] [Linear] [Name("Occlusion Map")]
[[vk::binding(5,1)]]
Sampler2D gOcclusionMap;

[Reflect] [Name("Emissive Map")]
[[vk::binding(6,1)]]
Sampler2D gEmissiveMap;

float channelOf(float4 value, float index)
{
    if (index >= 2.5)
    {
        return value.a;
    }
    if (index >= 1.5)
    {
        return value.b;
    }
    if (index >= 0.5)
    {
        return value.g;
    }
    return value.r;
}

// Mirrors VulkanRenderer::InstanceData
struct InstanceData
{
    float4x4 model;

    uint jointOffset;

    // Scalars not uint3 since std430 aligns uint3 to 16
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

    [[vk::location(3)]]
    float3 worldPosition : TEXCOORD2;

    [[vk::location(4)]]
    float viewSpaceZ : TEXCOORD3;

    [[vk::location(5)]]
    float3 worldTangent : TANGENT;

    [[vk::location(6)]]
    float tangentSign : TEXCOORD4;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input, uint instanceID : SV_InstanceID)
{
    VSOutput output;

    // firstInstance is always 0 so the run start comes from the push constant
    float4x4 model = gInstances[gPush.instanceBase + instanceID].model;

    float4 worldPos = mul(model, float4(input.position, 1.0));

    output.position =
        mul(
            gCamera.viewProjection,
            worldPos);

    output.worldPosition = worldPos.xyz;
    output.viewSpaceZ = mul(gCamera.view, worldPos).z;

    output.worldNormal =
        normalize(
            mul(
                (float3x3)model,
                input.normal));

    output.worldTangent =
        normalize(
            mul(
                (float3x3)model,
                input.tangent.xyz));
    output.tangentSign = input.tangent.w;

    output.uv =
        input.uv;

    output.color = input.color;

    return output;
}

// inout not a return so the write lands before any debug view returns
float4 shadeSurface(VSOutput input, inout float4 outNormalRoughness, inout float3 outIndirectDiffuse)
{
    outNormalRoughness = float4(0.0, 0.0, 0.0, 0.0);
    outIndirectDiffuse = float3(0.0, 0.0, 0.0);

    float4 albedoSample = gAlbedo.Sample(input.uv);
    float alpha = albedoSample.a * gMaterial.tint.a;

    // The alpha test lives in fragmentMainCutout since a reachable discard forces late Z

    float3 albedo = albedoSample.rgb * gMaterial.tint.rgb * input.color;
    float metallic = clamp(gMaterial.metallic * channelOf(gMetallicMap.Sample(input.uv), gMaterial.metallicChannel), 0.0, 1.0);
    float roughness = clamp(gMaterial.roughness * channelOf(gRoughnessMap.Sample(input.uv), gMaterial.roughnessChannel), 0.045, 1.0);
    float occlusion = lerp(
        1.0,
        channelOf(gOcclusionMap.Sample(input.uv), gMaterial.occlusionChannel),
        clamp(gMaterial.occlusionStrength, 0.0, 1.0)
    );

    float3 N = normalize(input.worldNormal);
    float3 T = input.worldTangent - N * dot(N, input.worldTangent);
    float tangentLength = length(T);
    float handedness = input.tangentSign;

    // glTF only requires tangents with a normal map and a zero tangent normalizes to NaN
    if (tangentLength < 1e-6 || abs(handedness) < 1e-6)
    {
        float3 reference = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
        T = normalize(cross(reference, N));
        handedness = 1.0;
    }
    else
    {
        T = T / tangentLength;
    }

    float3 B = cross(N, T) * handedness;

    float3 geometricNormal = N;

    float3 tangentNormal = gNormal.Sample(input.uv).rgb * 2.0 - 1.0;
    N = normalize(T * tangentNormal.x + B * tangentNormal.y + N * tangentNormal.z);

    outNormalRoughness = float4(N, roughness);

    float3 V = normalize(gCamera.position - input.worldPosition);

    Surface surface;
    surface.worldPosition = input.worldPosition;
    surface.normal = N;
    surface.geometricNormal = geometricNormal;
    surface.viewDirection = V;
    surface.albedo = albedo;
    surface.metallic = metallic;
    surface.roughness = roughness;
    surface.occlusion = occlusion;
    surface.viewSpaceZ = input.viewSpaceZ;
    surface.pixel = input.position.xy;

    ShadingResult lit = shadeSurfaceLighting(surface);
    outIndirectDiffuse = lit.indirectDiffuse;

    if (lit.isDebugView)
    {
        return float4(lit.color, 1.0);
    }

    float3 color = lit.color;

    color += gMaterial.emissiveColor * gMaterial.emissiveIntensity * gEmissiveMap.Sample(input.uv).rgb;

    return float4(color, alpha);
}

// Struct since Slang assigns out parameters by declaration order and ignores SV_Target on them
struct FragmentOutput
{
    float4 color : SV_Target0;
    float4 normalRoughness : SV_Target1;
    float4 indirectDiffuse : SV_Target2;
};

// Two entry points since a reachable discard forces late Z even behind a bool parameter
// Check with slangc mesh.slang -target spirv-assembly -entry fragmentMain -stage fragment | grep OpKill
[shader("fragment")]
FragmentOutput fragmentMain(VSOutput input)
{
    FragmentOutput output;
    output.normalRoughness = float4(0.0, 0.0, 0.0, 0.0);

    float3 indirectDiffuse = float3(0.0, 0.0, 0.0);
    output.color = shadeSurface(input, output.normalRoughness, indirectDiffuse);
    output.indirectDiffuse = float4(indirectDiffuse, 1.0);
    return output;
}

[shader("fragment")]
FragmentOutput fragmentMainCutout(VSOutput input)
{
    float4 albedoSample = gAlbedo.Sample(input.uv);
    float alpha = albedoSample.a * gMaterial.tint.a;
    if (gMaterial.alphaCutoff > 0.0 && alpha < gMaterial.alphaCutoff)
    {
        discard;
    }

    FragmentOutput output;
    output.normalRoughness = float4(0.0, 0.0, 0.0, 0.0);

    float3 indirectDiffuse = float3(0.0, 0.0, 0.0);
    output.color = shadeSurface(input, output.normalRoughness, indirectDiffuse);
    output.indirectDiffuse = float4(indirectDiffuse, 1.0);
    return output;
}
