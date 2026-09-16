import fullscreen;

struct TonemapParams
{
    float exposure;

    // 0 Reinhard 1 ACES
    uint mode;

    float gamma;

    float contrast;
    float saturation;

    float vignette;

    float grain;

    float time;

    // std140 rounds this block to 48 bytes and the C++ struct pads to match
    float _pad0;
};

[[vk::binding(0,0)]]
ConstantBuffer<TonemapParams> gParams;

[[vk::binding(1,0)]]
Sampler2D gSceneColor;

typedef FullscreenVSOutput VSOutput;

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    return fullscreenTriangle(vertexId);
}

float luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float3 tonemapReinhard(float3 color)
{
    return color / (color + float3(1.0, 1.0, 1.0));
}

float3 tonemapACES(float3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

float ditherOffset(float2 pixel)
{
    float noise = frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
    return (noise - 0.5) / 255.0;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    float3 color = gSceneColor.Sample(input.uv).rgb * gParams.exposure;

    color = gParams.mode == 1 ? tonemapACES(color) : tonemapReinhard(color);

    const float kMiddleGrey = 0.18;
    color = max((color - kMiddleGrey) * gParams.contrast + kMiddleGrey, float3(0.0, 0.0, 0.0));

    color = max(lerp(float3(luminance(color), luminance(color), luminance(color)), color, gParams.saturation),
                float3(0.0, 0.0, 0.0));

    color = pow(max(color, float3(0.0, 0.0, 0.0)), float3(1.0 / gParams.gamma, 1.0 / gParams.gamma, 1.0 / gParams.gamma));

    if (gParams.vignette > 0.0)
    {
        float2 centered = input.uv - 0.5;
        float falloff = 1.0 - saturate(dot(centered, centered) * gParams.vignette * 2.0);
        color *= falloff * falloff;
    }

    if (gParams.grain > 0.0)
    {
        float noise = frac(sin(dot(input.uv + frac(gParams.time), float2(12.9898, 78.233))) * 43758.5453);
        color += (noise - 0.5) * gParams.grain;
    }

    color += ditherOffset(input.position.xy);

    return float4(color, 1.0);
}
