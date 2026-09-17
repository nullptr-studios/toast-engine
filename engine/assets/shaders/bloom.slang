import fullscreen;

struct BloomParams
{
    float2 inverseSourceSize;

    float threshold;

    float knee;

    float filterRadius;

    float strength;

    float2 _pad0;
};

[[vk::push_constant]]
BloomParams gParams;

[[vk::binding(0,0)]]
Sampler2D gSource;

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

float3 applyThreshold(float3 color)
{
    float brightness = max(color.r, max(color.g, color.b));
    float knee = max(gParams.knee, 1e-4);
    float soft = clamp(brightness - gParams.threshold + knee, 0.0, 2.0 * knee);
    soft = (soft * soft) / (4.0 * knee);
    float contribution = max(soft, brightness - gParams.threshold) / max(brightness, 1e-4);
    return color * contribution;
}

// Not an entry point or Slang would promote the uniform parameter into a second push block
float4 fragmentDownsample(VSOutput input, uniform bool applyKaris) : SV_Target0
{
    float2 texel = gParams.inverseSourceSize;
    float2 uv = input.uv;

    float3 a = gSource.Sample(uv + float2(-2.0, 2.0) * texel).rgb;
    float3 b = gSource.Sample(uv + float2( 0.0, 2.0) * texel).rgb;
    float3 c = gSource.Sample(uv + float2( 2.0, 2.0) * texel).rgb;
    float3 d = gSource.Sample(uv + float2(-2.0, 0.0) * texel).rgb;
    float3 e = gSource.Sample(uv).rgb;
    float3 f = gSource.Sample(uv + float2( 2.0, 0.0) * texel).rgb;
    float3 g = gSource.Sample(uv + float2(-2.0,-2.0) * texel).rgb;
    float3 h = gSource.Sample(uv + float2( 0.0,-2.0) * texel).rgb;
    float3 i = gSource.Sample(uv + float2( 2.0,-2.0) * texel).rgb;

    float3 j = gSource.Sample(uv + float2(-1.0, 1.0) * texel).rgb;
    float3 k = gSource.Sample(uv + float2( 1.0, 1.0) * texel).rgb;
    float3 l = gSource.Sample(uv + float2(-1.0,-1.0) * texel).rgb;
    float3 m = gSource.Sample(uv + float2( 1.0,-1.0) * texel).rgb;

    float3 group0 = (j + k + l + m) * 0.25;
    float3 group1 = (a + b + d + e) * 0.25;
    float3 group2 = (b + c + e + f) * 0.25;
    float3 group3 = (d + e + g + h) * 0.25;
    float3 group4 = (e + f + h + i) * 0.25;

    float3 result;
    if (applyKaris)
    {
        float w0 = 1.0 / (1.0 + luminance(group0));
        float w1 = 1.0 / (1.0 + luminance(group1));
        float w2 = 1.0 / (1.0 + luminance(group2));
        float w3 = 1.0 / (1.0 + luminance(group3));
        float w4 = 1.0 / (1.0 + luminance(group4));
        float total = w0 * 0.5 + (w1 + w2 + w3 + w4) * 0.125;
        result = (group0 * w0 * 0.5 + (group1 * w1 + group2 * w2 + group3 * w3 + group4 * w4) * 0.125) / max(total, 1e-4);

        result = applyThreshold(result);
    }
    else
    {
        result = group0 * 0.5 + (group1 + group2 + group3 + group4) * 0.125;
    }

    return float4(result, 1.0);
}

[shader("fragment")]
float4 fragmentDownsampleFirst(VSOutput input) : SV_Target0
{
    return fragmentDownsample(input, true);
}

[shader("fragment")]
float4 fragmentDownsampleNext(VSOutput input) : SV_Target0
{
    return fragmentDownsample(input, false);
}

[shader("fragment")]
float4 fragmentUpsample(VSOutput input) : SV_Target0
{
    float2 offset = gParams.inverseSourceSize * gParams.filterRadius;
    float2 uv = input.uv;

    float3 a = gSource.Sample(uv + float2(-1.0,  1.0) * offset).rgb;
    float3 b = gSource.Sample(uv + float2( 0.0,  1.0) * offset).rgb;
    float3 c = gSource.Sample(uv + float2( 1.0,  1.0) * offset).rgb;
    float3 d = gSource.Sample(uv + float2(-1.0,  0.0) * offset).rgb;
    float3 e = gSource.Sample(uv).rgb;
    float3 f = gSource.Sample(uv + float2( 1.0,  0.0) * offset).rgb;
    float3 g = gSource.Sample(uv + float2(-1.0, -1.0) * offset).rgb;
    float3 h = gSource.Sample(uv + float2( 0.0, -1.0) * offset).rgb;
    float3 i = gSource.Sample(uv + float2( 1.0, -1.0) * offset).rgb;

    float3 result = e * 4.0;
    result += (b + d + f + h) * 2.0;
    result += (a + c + g + i);
    result *= 1.0 / 16.0;

    return float4(result, 1.0);
}

[shader("fragment")]
float4 fragmentComposite(VSOutput input) : SV_Target0
{
    float3 scene = gSceneColor.Sample(input.uv).rgb;
    float3 bloom = gSource.Sample(input.uv).rgb;
    return float4(scene + bloom * gParams.strength, 1.0);
}
