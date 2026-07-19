struct CameraUBO {
    float4x4 view;
    float4x4 projection;
    float4x4 viewProjection;

    float3 cameraPosition;
    float time;
};

[[vk::binding(0,0)]]
ConstantBuffer<CameraUBO> gCamera;

[[vk::binding(0,1)]]
Sampler2D gPanelTexture;

struct WorldPush {
    float4x4 model;
};

[[vk::push_constant]]
WorldPush gWorldPush;

struct VSOutput
{
    float4 position : SV_Position;

    [[vk::location(0)]]
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertex_id : SV_VertexID) {
    static const float2 corners[6] = {
        float2(-0.5,  0.5), float2( 0.5,  0.5), float2( 0.5, -0.5),
        float2(-0.5,  0.5), float2( 0.5, -0.5), float2(-0.5, -0.5),
    };

    float2 corner = corners[vertex_id];

    VSOutput output;
    float4 world_pos = mul(gWorldPush.model, float4(corner.x, 0.0, corner.y, 1.0));
    output.position = mul(gCamera.viewProjection, world_pos);
    output.uv = float2(corner.x + 0.5, 0.5 - corner.y);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0 {
    return gPanelTexture.Sample(input.uv);
}
