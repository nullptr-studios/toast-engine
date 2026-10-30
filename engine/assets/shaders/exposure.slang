// Mirrors TonemapPass::ExposureParams
struct ExposureParams
{
    float minLog;
    float logRange;
    float deltaTime;

    // 1 jumps straight to the metered value
    float snap;

    float speedUp;
    float speedDown;

    uint width;
    uint height;
};

[[vk::push_constant]]
ConstantBuffer<ExposureParams> gParams;

[[vk::binding(0, 0)]]
Sampler2D gSource;

[[vk::binding(1, 0)]]
RWStructuredBuffer<uint> gHistogram;

// x adapted log2 luminance y exposure the tonemap multiplies by
[[vk::binding(2, 0)]]
RWStructuredBuffer<float> gExposure;

static const uint kBins = 256;

static const float kMiddleGrey = 0.18;

// Trims both tails so one bright or dark pixel does not swing the exposure
static const float kLowPercentile = 0.1;
static const float kHighPercentile = 0.9;

groupshared uint sBins[kBins];
groupshared float2 sSums[kBins];

float luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

[shader("compute")]
[numthreads(16, 16, 1)]
void histogramMain(uint3 id : SV_DispatchThreadID, uint index : SV_GroupIndex)
{
    sBins[index] = 0u;
    GroupMemoryBarrierWithGroupSync();

    if (id.x < gParams.width && id.y < gParams.height)
    {
        float lum = luminance(gSource.Load(int3(id.xy, 0)).rgb);

        // The comparison also sends NaN to the darkest bin
        float t = lum > 0.0 ? saturate((log2(lum) - gParams.minLog) / gParams.logRange) : 0.0;
        InterlockedAdd(sBins[min(uint(t * float(kBins)), kBins - 1u)], 1u);
    }
    GroupMemoryBarrierWithGroupSync();

    if (sBins[index] != 0u)
    {
        InterlockedAdd(gHistogram[index], sBins[index]);
    }
}

[shader("compute")]
[numthreads(256, 1, 1)]
void adaptMain(uint index : SV_GroupIndex)
{
    const uint count = gHistogram[index];
    sBins[index] = count;
    GroupMemoryBarrierWithGroupSync();

    for (uint offset = 1u; offset < kBins; offset <<= 1u)
    {
        const uint below = index >= offset ? sBins[index - offset] : 0u;
        GroupMemoryBarrierWithGroupSync();
        sBins[index] += below;
        GroupMemoryBarrierWithGroupSync();
    }

    const float total = float(sBins[kBins - 1u]);
    const float seen = float(sBins[index] - count);
    const float low = total * kLowPercentile;
    const float high = total * kHighPercentile;
    const float inside = max(min(seen + float(count), high) - max(seen, low), 0.0);
    sSums[index] = float2(inside * (gParams.minLog + (float(index) + 0.5) / float(kBins) * gParams.logRange), inside);
    GroupMemoryBarrierWithGroupSync();

    for (uint stride = kBins / 2u; stride > 0u; stride >>= 1u)
    {
        if (index < stride)
        {
            sSums[index] += sSums[index + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (index != 0u)
    {
        return;
    }
    const float weighted = sSums[0].x;
    const float counted = sSums[0].y;
    float target = counted > 0.0 ? weighted / counted : gParams.minLog + 0.5 * gParams.logRange;
    float previous = gExposure[0];
    float speed = target > previous ? gParams.speedUp : gParams.speedDown;
    float adapted = previous + (target - previous) * (1.0 - exp(-gParams.deltaTime * speed));

    // The buffer starts undefined and a NaN would never adapt away
    if (gParams.snap > 0.5 || isnan(adapted))
    {
        adapted = target;
    }

    gExposure[0] = adapted;
    gExposure[1] = kMiddleGrey / exp2(adapted);
}
