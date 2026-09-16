import fullscreen;

[[vk::binding(0,0)]]
Sampler2D gSource;

typedef FullscreenVSOutput VSOutput;

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    return fullscreenTriangle(vertexId);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    return float4(gSource.Sample(input.uv).rgb, 1.0);
}
