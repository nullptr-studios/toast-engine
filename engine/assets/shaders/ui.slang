[[vk::binding(0,0)]]
Sampler2D gUITexture;

struct VSOutput {
    float4 position : SV_Position;

    [[vk::location(0)]]
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertex_id : SV_VertexID) {
    VSOutput output;
    output.uv = float2((vertex_id << 1) & 2, vertex_id & 2);
    output.position = float4(output.uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0 {
    return gUITexture.Sample(input.uv);
}
