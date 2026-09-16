import fullscreen;

struct FxaaParams
{
    float2 inverseSourceSize;

    float contrastThreshold;

    float relativeThreshold;

    float subpixelBlending;

    // float2 not float3 since a 3 component pad aligns to 16 and grows the block past 32 bytes
    float2 _pad0;
};

[[vk::push_constant]]
FxaaParams gParams;

[[vk::binding(0,0)]]
Sampler2D gSource;

typedef FullscreenVSOutput VSOutput;

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    return fullscreenTriangle(vertexId);
}

// Green approximates luma and FXAA is tuned against it
float sampleLuma(float2 uv)
{
    return gSource.Sample(uv).g;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    float2 texel = gParams.inverseSourceSize;
    float2 uv = input.uv;

    float lumaCenter = sampleLuma(uv);
    float lumaNorth = sampleLuma(uv + float2(0.0, -1.0) * texel);
    float lumaSouth = sampleLuma(uv + float2(0.0, 1.0) * texel);
    float lumaWest = sampleLuma(uv + float2(-1.0, 0.0) * texel);
    float lumaEast = sampleLuma(uv + float2(1.0, 0.0) * texel);

    float lumaMin = min(lumaCenter, min(min(lumaNorth, lumaSouth), min(lumaWest, lumaEast)));
    float lumaMax = max(lumaCenter, max(max(lumaNorth, lumaSouth), max(lumaWest, lumaEast)));
    float lumaRange = lumaMax - lumaMin;

    if (lumaRange < max(gParams.contrastThreshold, lumaMax * gParams.relativeThreshold))
    {
        return float4(gSource.Sample(uv).rgb, 1.0);
    }

    float lumaNorthWest = sampleLuma(uv + float2(-1.0, -1.0) * texel);
    float lumaNorthEast = sampleLuma(uv + float2(1.0, -1.0) * texel);
    float lumaSouthWest = sampleLuma(uv + float2(-1.0, 1.0) * texel);
    float lumaSouthEast = sampleLuma(uv + float2(1.0, 1.0) * texel);

    float lumaNS = lumaNorth + lumaSouth;
    float lumaWE = lumaWest + lumaEast;
    float edgeHorizontal =
        abs(lumaNorthWest + lumaSouthWest - 2.0 * lumaWest) * 2.0 +
        abs(lumaNorth + lumaSouth - 2.0 * lumaCenter) +
        abs(lumaNorthEast + lumaSouthEast - 2.0 * lumaEast) * 2.0;
    float edgeVertical =
        abs(lumaNorthWest + lumaNorthEast - 2.0 * lumaNorth) * 2.0 +
        abs(lumaWest + lumaEast - 2.0 * lumaCenter) +
        abs(lumaSouthWest + lumaSouthEast - 2.0 * lumaSouth) * 2.0;

    bool horizontal = edgeHorizontal >= edgeVertical;

    float luma1 = horizontal ? lumaNorth : lumaWest;
    float luma2 = horizontal ? lumaSouth : lumaEast;
    float gradient1 = abs(luma1 - lumaCenter);
    float gradient2 = abs(luma2 - lumaCenter);

    float stepLength = horizontal ? texel.y : texel.x;
    if (gradient1 < gradient2)
    {
        stepLength = -stepLength;
    }

    float lumaAverage = (2.0 * (lumaNS + lumaWE) + lumaNorthWest + lumaNorthEast + lumaSouthWest + lumaSouthEast) / 12.0;
    float subpixel = saturate(abs(lumaAverage - lumaCenter) / max(lumaRange, 1e-4));
    subpixel = smoothstep(0.0, 1.0, subpixel);
    subpixel = subpixel * subpixel * gParams.subpixelBlending;

    float2 blendUv = uv;
    if (horizontal)
    {
        blendUv.y += stepLength * subpixel;
    }
    else
    {
        blendUv.x += stepLength * subpixel;
    }

    return float4(gSource.Sample(blendUv).rgb, 1.0);
}
