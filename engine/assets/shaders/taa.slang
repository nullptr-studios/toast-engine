import fullscreen;
import depth_convention;

struct TaaParams
{
    // Current unjittered clip to previous clip when motion is not written
    float4x4 reprojection;
    // xy 1 over size zw size
    float4 texel;
    // x history weight y 1 resets history z 1 ignores written motion w 1 draws the motion debug view
    float4 tuning;
};

[[vk::push_constant]]
ConstantBuffer<TaaParams> gParams;

[[vk::binding(0, 0)]]
Sampler2D gCurrent;

[[vk::binding(1, 0)]]
Sampler2D gHistory;

[[vk::binding(2, 0)]]
Sampler2D gDepth;

[[vk::binding(3, 0)]]
Sampler2D gMotion;

typedef FullscreenVSOutput VSOutput;

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    return fullscreenTriangle(vertexId);
}

// Karis reversible tonemap so one firefly cannot drag the history
float3 compress(float3 color)
{
    return color / (1.0 + max(color.r, max(color.g, color.b)));
}

// The clipped history can leave the unit cube where the inverse blows up
float3 expand(float3 color)
{
    color = clamp(color, 0.0, 0.9999);
    return color / (1.0 - max(color.r, max(color.g, color.b)));
}

float3 toYCoCg(float3 c)
{
    return float3(dot(c, float3(0.25, 0.5, 0.25)), dot(c, float3(0.5, 0.0, -0.5)), dot(c, float3(-0.25, 0.5, -0.25)));
}

float3 fromYCoCg(float3 c)
{
    return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

// Five tap Catmull-Rom with the corners dropped
float3 sampleHistory(float2 uv)
{
    float2 size = gParams.texel.zw;
    float2 position = uv * size;
    float2 center = floor(position - 0.5) + 0.5;
    float2 f = position - center;

    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);

    float2 w12 = w1 + w2;
    float2 uv0 = (center - 1.0) * gParams.texel.xy;
    float2 uv3 = (center + 2.0) * gParams.texel.xy;
    float2 uv12 = (center + w2 / w12) * gParams.texel.xy;

    float3 sum = gHistory.SampleLevel(float2(uv12.x, uv0.y), 0.0).rgb * (w12.x * w0.y);
    sum += gHistory.SampleLevel(float2(uv0.x, uv12.y), 0.0).rgb * (w0.x * w12.y);
    sum += gHistory.SampleLevel(uv12, 0.0).rgb * (w12.x * w12.y);
    sum += gHistory.SampleLevel(float2(uv3.x, uv12.y), 0.0).rgb * (w3.x * w12.y);
    sum += gHistory.SampleLevel(float2(uv12.x, uv3.y), 0.0).rgb * (w12.x * w3.y);

    float weight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(sum / weight, float3(0.0, 0.0, 0.0));
}

float3 clipToBox(float3 value, float3 boxMin, float3 boxMax)
{
    float3 center = 0.5 * (boxMax + boxMin);
    float3 halfExtent = 0.5 * (boxMax - boxMin) + 1e-5;
    float3 offset = value - center;
    float3 units = abs(offset / halfExtent);
    float largest = max(units.x, max(units.y, units.z));
    return largest > 1.0 ? center + offset / largest : value;
}

float3 motionColor(float4 motion)
{
    if (motion.w < 0.5)
    {
        return float3(0.15, 0.15, 0.15) + motion.z * float3(0.6, 0.0, 0.6);
    }
    // Hue is direction and brightness is distance moved
    float2 pixels = motion.xy * gParams.texel.zw;
    float hue = atan2(pixels.y, pixels.x) / (2.0 * 3.14159265) + 0.5;
    float3 rgb = saturate(abs(frac(hue + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0);
    return rgb * saturate(length(pixels) / 8.0);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    float2 uv = input.uv;
    float2 texel = gParams.texel.xy;

    float3 center = float3(0.0, 0.0, 0.0);
    float3 moment1 = float3(0.0, 0.0, 0.0);
    float3 moment2 = float3(0.0, 0.0, 0.0);
    float closestDepth = kFarDepth;
    float2 closestUv = uv;

    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            float2 sampleUv = uv + float2(x, y) * texel;
            float3 value = toYCoCg(compress(gCurrent.SampleLevel(sampleUv, 0.0).rgb));
            moment1 += value;
            moment2 += value * value;
            if (x == 0 && y == 0)
            {
                center = value;
            }

            // Keeps a moving edge from trailing the background
            float depth = gDepth.SampleLevel(sampleUv, 0.0).r;
            if (isCloserDepth(depth, closestDepth))
            {
                closestDepth = depth;
                closestUv = sampleUv;
            }
        }
    }

    float4 motion = gMotion.SampleLevel(closestUv, 0.0);

    if (gParams.tuning.w > 0.5)
    {
        return float4(motionColor(motion), 1.0);
    }

    float2 previousUv;
    if (motion.w > 0.5 && gParams.tuning.z < 0.5)
    {
        previousUv = uv - motion.xy;
    }
    else
    {
        float4 previousClip = mul(gParams.reprojection, float4(closestUv * 2.0 - 1.0, closestDepth, 1.0));
        previousUv = uv + previousClip.xy / previousClip.w * 0.5 + 0.5 - closestUv;
    }

    bool offscreen = any(previousUv < float2(0.0, 0.0)) || any(previousUv > float2(1.0, 1.0));
    if (gParams.tuning.y > 0.5 || offscreen)
    {
        return float4(expand(fromYCoCg(center)), 1.0);
    }

    float3 mean = moment1 / 9.0;
    float3 sigma = sqrt(max(moment2 / 9.0 - mean * mean, float3(0.0, 0.0, 0.0)));
    float3 history = toYCoCg(compress(sampleHistory(previousUv)));
    history = clipToBox(history, mean - sigma, mean + sigma);

    float historyWeight = gParams.tuning.x * (1.0 - saturate(motion.z));
    float3 resolved = lerp(center, history, historyWeight);

    // A NaN would otherwise live in the history until the next reset
    if (any(isnan(resolved)))
    {
        resolved = center;
    }
    return float4(expand(fromYCoCg(resolved)), 1.0);
}
