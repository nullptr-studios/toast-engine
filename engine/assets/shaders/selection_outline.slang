// The mask carries the TAA jitter so the outline is averaged over frames before it reaches the output
import fullscreen;

/// Stencil aspect of the selection_mask.slang target
[[vk::binding(0, 0)]]
Sampler2D<uint> gMask;

/// r solid coverage g hidden coverage
[[vk::binding(1, 0)]]
Sampler2D gHistory;

[[vk::binding(2, 0)]]
Sampler2D gOutline;

/// Mirrors renderer::SelectionOutline::OutlinePush
struct PushConstants
{
    float4 solidColor;
    float4 hiddenColor;
    /// Pixels from xy up to zw the resolve may cover
    int4 rect;
    /// Weight of this frame where 1 drops the history
    float blend;
    float pad0;
    float pad1;
    float pad2;
};

[[vk::push_constant]]
PushConstants gPush;

static const uint kSilhouette = 1u;
static const uint kVisible = 2u;

static const int kWidth = 2;
static const int kDashPixels = 4;

typedef FullscreenVSOutput VSOutput;

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    return fullscreenTriangle(vertexId);
}

/// Solid where the outline borders a visible part and dashed where only hidden parts are around
float2 outlineCoverage(int2 pixel)
{
    const uint center = gMask.Load(int3(pixel, 0)).r;
    if ((center & kVisible) != 0u)
    {
        return float2(0.0, 0.0);
    }

    uint2 size;
    gMask.GetDimensions(size.x, size.y);
    const int2 last = int2(size) - 1;

    uint around = 0u;
    [unroll]
    for (int y = -kWidth; y <= kWidth; ++y)
    {
        [unroll]
        for (int x = -kWidth; x <= kWidth; ++x)
        {
            if (x * x + y * y <= kWidth * kWidth)
            {
                around |= gMask.Load(int3(clamp(pixel + int2(x, y), int2(0, 0), last), 0)).r;
            }
        }
    }

    if ((around & kVisible) != 0u)
    {
        return float2(1.0, 0.0);
    }

    // A hidden pixel of the selection only draws where a visible part meets it
    const bool hidden = (center & kSilhouette) != 0u;
    if (hidden || (around & kSilhouette) == 0u || ((pixel.x / kDashPixels + pixel.y / kDashPixels) & 1) != 0)
    {
        return float2(0.0, 0.0);
    }
    return float2(0.0, 1.0);
}

[shader("fragment")]
float2 resolveMain(VSOutput input) : SV_Target0
{
    const int2 pixel = int2(input.position.xy);
    if (any(pixel < gPush.rect.xy) || any(pixel >= gPush.rect.zw))
    {
        return float2(0.0, 0.0);
    }
    const float2 current = outlineCoverage(pixel);
    return lerp(gHistory.Load(int3(pixel, 0)).rg, current, gPush.blend);
}

[shader("fragment")]
float4 compositeMain(VSOutput input) : SV_Target0
{
    const float2 coverage = gOutline.Load(int3(int2(input.position.xy), 0)).rg;
    if (coverage.x + coverage.y < 1.0 / 255.0)
    {
        discard;
    }

    // Premultiplied
    return float4(
        gPush.solidColor.rgb * coverage.x + gPush.hiddenColor.rgb * coverage.y, saturate(coverage.x + coverage.y));
}
