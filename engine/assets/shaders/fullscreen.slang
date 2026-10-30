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

public float4 uiToDisplay(float4 premultiplied, float hdrPaperWhiteScale)
{
    if (hdrPaperWhiteScale <= 0.0)
    {
        return premultiplied;
    }

    const float alpha = premultiplied.a;
    const float3 encoded = alpha > 1e-5 ? premultiplied.rgb / alpha : float3(0.0, 0.0, 0.0);
    const float3 low = encoded / 12.92;
    const float3 high = pow((encoded + 0.055) / 1.055, float3(2.4, 2.4, 2.4));
    const float3 linearColor = float3(
        encoded.x <= 0.04045 ? low.x : high.x,
        encoded.y <= 0.04045 ? low.y : high.y,
        encoded.z <= 0.04045 ? low.z : high.z);
    return float4(linearColor * hdrPaperWhiteScale * alpha, alpha);
}
