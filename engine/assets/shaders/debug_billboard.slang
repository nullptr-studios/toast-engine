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

[[vk::binding(0,1)]]
Sampler2D gIcon;

struct PushConstants
{
    float4 centerSize;  // xyz centre w edge length
    float4 tint;
};

[[vk::push_constant]]
PushConstants gPush;

struct VSOutput
{
    float4 position : SV_Position;

    [[vk::location(0)]]
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    const float2 offsets[6] = {
        float2(-1.0, -1.0), float2( 1.0, -1.0), float2( 1.0,  1.0),
        float2(-1.0, -1.0), float2( 1.0,  1.0), float2(-1.0,  1.0),
    };

    float2 offset = offsets[vertexId];

    float3 center = gPush.centerSize.xyz;
    float halfSize = gPush.centerSize.w * 0.5;

    // Camera position not the view matrix because glm is column major and Slang row major
    float3 forward = center - gCamera.cameraPosition;
    float forwardLen = length(forward);
    forward = forwardLen > 0.0001 ? forward / forwardLen : float3(0.0, 1.0, 0.0);

    float3 up = float3(0.0, 0.0, 1.0);
    if (abs(dot(forward, up)) > 0.999)
    {
        up = float3(0.0, 1.0, 0.0);
    }

    float3 right = normalize(cross(forward, up));
    up = cross(right, forward);

    float3 worldPos = center + right * (offset.x * halfSize) + up * (offset.y * halfSize);

    VSOutput output;
    output.position = mul(gCamera.viewProjection, float4(worldPos, 1.0));
    output.uv = float2(offset.x * 0.5 + 0.5, 0.5 - offset.y * 0.5);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    float4 icon = gIcon.Sample(input.uv) * gPush.tint;

    if (icon.a < 0.01)
    {
        discard;
    }

    return icon;
}
