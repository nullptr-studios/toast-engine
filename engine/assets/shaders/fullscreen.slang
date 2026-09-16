// No Y flip because Camera::getProjection() negates proj[1][1]

public struct FullscreenVSOutput
{
    public float4 position : SV_Position;

    [[vk::location(0)]]
    public float2 uv : TEXCOORD0;
};

public FullscreenVSOutput fullscreenTriangle(uint vertexId)
{
    FullscreenVSOutput output;
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}
