// Lens flare after Froyok's UE4 custom flare, ghosts and halo from a chroma shifted threshold, glare as luminance scaled streak quads
import fullscreen;

struct LensFlareParams
{
    // xy 1 over the source size zw 1 over the target size
    float4 texel;
    // x threshold level y threshold range z ghost intensity w chroma shift
    float4 flare;
    // x halo intensity y halo width z halo mask w halo compression
    float4 halo;
    // x glare intensity y glare divider z aspect
    float4 glare;
    // rgb tint w overall intensity
    float4 tint;
    // x anamorphic intensity y anamorphic tint amount z dirt intensity w 1 with a dirt texture
    float4 streak;
    // x halo thickness y halo dispersion
    float4 haloStyle;
    // rgb halo tint
    float4 haloTint;
};

[[vk::push_constant]]
ConstantBuffer<LensFlareParams> gParams;

[[vk::binding(0, 0)]]
Sampler2D gSource;

[[vk::binding(1, 0)]]
Sampler2D gScene;

[[vk::binding(2, 0)]]
Sampler2D gGlare;

[[vk::binding(3, 0)]]
Sampler2D gStreak;

[[vk::binding(4, 0)]]
Sampler2D gDirtTexture;

typedef FullscreenVSOutput VSOutput;

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    return fullscreenTriangle(vertexId);
}

static const float2 kCenter = float2(0.5, 0.5);
static const uint kGhostCount = 8u;
static const float kGhostScales[8] = {-1.5, 2.5, -5.0, 10.0, 0.7, -0.4, -0.2, -0.1};
static const float4 kGhostColors[8] = {
    float4(1.0, 0.85, 0.7, 0.8), float4(0.6, 0.8, 1.0, 0.6), float4(1.0, 0.6, 0.7, 0.5), float4(0.7, 1.0, 0.8, 0.4),
    float4(1.0, 0.9, 0.5, 0.6), float4(0.6, 0.7, 1.0, 0.5), float4(1.0, 0.7, 0.5, 0.4), float4(0.8, 0.6, 1.0, 0.5)};

static const float3 kStreakTint = float3(0.35, 0.62, 1.0);

/// Hexagonal aperture norm with the same scale as length
float hexLength(float2 p)
{
    return max(abs(p.x) * 0.8660254 + abs(p.y) * 0.5, abs(p.y)) * 1.0392305;
}

/// Disc that is 1 at the centre and 0 at radius 1 of the screen position in -1 to 1
float discMask(float2 screen)
{
    return saturate(1.0 - dot(screen, screen));
}

/// Smooth 13 tap box that keeps a moving highlight from flickering the flare, then a soft threshold
[shader("fragment")]
float4 fragmentThreshold(VSOutput input) : SV_Target0
{
    const float2 texel = gParams.texel.xy;
    const float2 uv = input.uv;

    float3 corners = float3(0.0, 0.0, 0.0);
    corners += gSource.Sample(uv + float2(-1.0, 1.0) * texel).rgb;
    corners += gSource.Sample(uv + float2(1.0, 1.0) * texel).rgb;
    corners += gSource.Sample(uv + float2(-1.0, -1.0) * texel).rgb;
    corners += gSource.Sample(uv + float2(1.0, -1.0) * texel).rgb;

    float3 grid = float3(0.0, 0.0, 0.0);
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            grid += gSource.Sample(uv + float2(float(x), float(y)) * 2.0 * texel).rgb;
        }
    }

    float3 color = corners * 0.25 * 0.5 + grid / 9.0 * 0.5;
    color = clamp(color, 0.0, 65000.0);

    const float luma = dot(color, float3(1.0, 1.0, 1.0));
    const float scale = saturate((luma - gParams.flare.x) / max(gParams.flare.y, 1e-4));
    return float4(color * scale, 1.0);
}

/// Dual Kawase down onto the next smaller target
[shader("fragment")]
float4 fragmentBlurDown(VSOutput input) : SV_Target0
{
    const float2 texel = gParams.texel.xy;
    const float2 uv = input.uv;
    float3 sum = gSource.Sample(uv).rgb * 4.0;
    sum += gSource.Sample(uv + float2(-1.0, -1.0) * texel).rgb;
    sum += gSource.Sample(uv + float2(1.0, -1.0) * texel).rgb;
    sum += gSource.Sample(uv + float2(-1.0, 1.0) * texel).rgb;
    sum += gSource.Sample(uv + float2(1.0, 1.0) * texel).rgb;
    return float4(sum / 8.0, 1.0);
}

[shader("fragment")]
float4 fragmentBlurUp(VSOutput input) : SV_Target0
{
    const float2 texel = gParams.texel.xy;
    const float2 uv = input.uv;
    float3 sum = float3(0.0, 0.0, 0.0);
    sum += gSource.Sample(uv + float2(-2.0, 0.0) * texel).rgb;
    sum += gSource.Sample(uv + float2(2.0, 0.0) * texel).rgb;
    sum += gSource.Sample(uv + float2(0.0, -2.0) * texel).rgb;
    sum += gSource.Sample(uv + float2(0.0, 2.0) * texel).rgb;
    sum += gSource.Sample(uv + float2(-1.0, 1.0) * texel).rgb * 2.0;
    sum += gSource.Sample(uv + float2(1.0, 1.0) * texel).rgb * 2.0;
    sum += gSource.Sample(uv + float2(-1.0, -1.0) * texel).rgb * 2.0;
    sum += gSource.Sample(uv + float2(1.0, -1.0) * texel).rgb * 2.0;
    return float4(sum / 12.0, 1.0);
}

[shader("fragment")]
float4 fragmentChroma(VSOutput input) : SV_Target0
{
    const float shift = gParams.flare.w;
    const float2 uv = input.uv;
    const float2 uvR = (uv - kCenter) * (1.0 + shift) + kCenter;
    const float2 uvB = (uv - kCenter) * (1.0 - shift) + kCenter;
    return float4(gSource.Sample(uvR).r, gSource.Sample(uv).g, gSource.Sample(uvB).b, 1.0);
}

[shader("fragment")]
float4 fragmentGhosts(VSOutput input) : SV_Target0
{
    const float2 uv = input.uv;
    const float2 screen = uv * 2.0 - 1.0;

    float3 color = float3(0.0, 0.0, 0.0);
    for (uint i = 0u; i < kGhostCount; ++i)
    {
        const float4 ghost = kGhostColors[i];
        const float2 ghostUv = (uv - kCenter) * kGhostScales[i];
        // Blades show on every other ghost
        const float distanceMask = 1.0 - ((i & 1u) == 0u ? hexLength(ghostUv) : length(ghostUv));
        const float mask = smoothstep(0.5, 0.9, distanceMask);
        const float edge = smoothstep(0.75, 1.0, distanceMask) * 0.95 + 0.05;
        color += gSource.Sample(ghostUv + kCenter).rgb * ghost.rgb * ghost.a * mask * edge;
    }
    return float4(color * discMask(screen * 0.9) * gParams.flare.z, 1.0);
}

/// Half heights from the centre so the halo shape does not depend on the aspect ratio
float2 heightSpace(float2 uv)
{
    return (uv * 2.0 - 1.0) * float2(gParams.glare.z, 1.0);
}

float2 fromHeightSpace(float2 p)
{
    return p / float2(gParams.glare.z, 1.0) * 0.5 + 0.5;
}

float2 fisheyeUv(float2 uv, float compression)
{
    const float2 centered = heightSpace(uv);
    const float scale = compression * atan(1.0 / compression);
    const float radius = length(centered);
    if (radius < 1e-5)
    {
        return uv;
    }
    const float bent = compression * tan(min(radius / compression, 1.5));
    return fromHeightSpace(centered / radius * bent / scale);
}

[shader("fragment")]
float4 fragmentHalo(VSOutput input) : SV_Target0
{
    const float shift = gParams.haloStyle.y;
    const float2 uv = input.uv;
    const float2 centered = heightSpace(uv);
    const float radius = length(centered);

    const float2 fish = fisheyeUv(uv, max(gParams.halo.w, 0.05));
    const float2 inward = -centered / max(radius, 1e-5);

    const float haloMask = smoothstep(gParams.halo.z, 1.0, saturate(radius));
    const float2 edge = centered / length(float2(gParams.glare.z, 1.0));
    const float border = (discMask(edge) * discMask(edge * 0.8)) * 0.95 + 0.05;

    // Taps along the radius thicken the ring
    float3 color = float3(0.0, 0.0, 0.0);
    for (int i = -2; i <= 2; ++i)
    {
        const float spread = 1.0 + float(i) * 0.25 * gParams.haloStyle.x;
        const float2 haloVector = fromHeightSpace(inward * gParams.halo.y * 2.0 * spread) - kCenter;
        const float2 uvR = (fish - kCenter) * (1.0 + shift) + kCenter + haloVector;
        const float2 uvG = fish + haloVector;
        const float2 uvB = (fish - kCenter) * (1.0 - shift) + kCenter + haloVector;
        color += float3(gSource.Sample(uvR).r, gSource.Sample(uvG).g, gSource.Sample(uvB).b) * (i == 0 ? 0.4 : 0.15);
    }
    return float4(color * border * haloMask * gParams.halo.x * gParams.haloTint.rgb, 1.0);
}

/// Horizontal only so a point stretches into an anamorphic streak, each level halves the width
[shader("fragment")]
float4 fragmentStreakDown(VSOutput input) : SV_Target0
{
    const float2 step = float2(gParams.texel.x, 0.0);
    const float2 uv = input.uv;
    float3 sum = gSource.Sample(uv - step * 1.5).rgb * 0.125;
    sum += gSource.Sample(uv - step * 0.5).rgb * 0.375;
    sum += gSource.Sample(uv + step * 0.5).rgb * 0.375;
    sum += gSource.Sample(uv + step * 1.5).rgb * 0.125;
    return float4(sum, 1.0);
}

/// Added over the finer level, the sum of every level is a long tailed streak
[shader("fragment")]
float4 fragmentStreakUp(VSOutput input) : SV_Target0
{
    const float2 step = float2(gParams.texel.x, 0.0);
    const float2 uv = input.uv;
    float3 sum = gSource.Sample(uv - step).rgb * 0.25;
    sum += gSource.Sample(uv).rgb * 0.5;
    sum += gSource.Sample(uv + step).rgb * 0.25;
    return float4(sum * 0.9, 1.0);
}

struct GlareVSOutput
{
    float4 position : SV_Position;

    [[vk::location(0)]]
    float2 local : TEXCOORD0;

    [[vk::location(1)]]
    float3 color : COLOR0;
};

static const float kPi = 3.14159265;

/// Three streak quads per bright tile, 18 vertices an instance and the dim ones collapse offscreen
[shader("vertex")]
GlareVSOutput glareVertex(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    GlareVSOutput output;
    output.local = float2(0.0, 0.0);
    output.color = float3(0.0, 0.0, 0.0);
    output.position = float4(2.0, 2.0, 0.0, 1.0);

    // One tile covers 2x2 texels of the threshold target
    uint width;
    uint height;
    gSource.GetDimensions(width, height);
    const uint tilesX = (width + 1u) / 2u;
    const uint2 tile = uint2(instanceId % tilesX, instanceId / tilesX);
    const float2 size = float2(float(width), float(height));
    const float2 uv = (float2(tile) * 2.0 + 1.0) / size;
    const float2 pixel = 1.0 / size;

    const float2 taps[5] = {float2(-1.0, 1.0), float2(1.0, 1.0), float2(0.0, 0.0), float2(-1.0, -1.0), float2(1.0, -1.0)};
    const float weights[5] = {0.175, 0.175, 0.3, 0.175, 0.175};
    float3 color = float3(0.0, 0.0, 0.0);
    for (int i = 0; i < 5; ++i)
    {
        color += gSource.SampleLevel(uv + taps[i] * pixel * 1.5, 0.0).rgb * weights[i];
    }

    const float luma = dot(color, float3(1.0, 1.0, 1.0));
    if (luma <= 0.1 || gParams.glare.x <= 0.0)
    {
        return output;
    }

    const uint quad = vertexId / 6u;
    const uint corner = vertexId % 6u;
    const uint cornerIndex[6] = {0u, 1u, 2u, 2u, 1u, 3u};
    const uint index = cornerIndex[corner];
    const float2 local = float2((index & 1u) == 0u ? -1.0 : 1.0, (index & 2u) == 0u ? -1.0 : 1.0);

    const float lumaScale = saturate(luma / max(gParams.glare.y, 0.01));
    float mask = 1.0 - saturate(distance(uv - 0.5, float2(0.0, 0.0)) * 2.0);
    mask = mask * 0.6 + 0.4;

    const float streak = lumaScale * mask;
    const float thickness = 4.0 / min(size.x, size.y) * 2.0;
    const float angle = (uv.x * 2.0 - 1.0) * (kPi / 6.0) + (quad == 0u ? kPi * 0.5 : (quad == 1u ? kPi / 6.0 : kPi * 5.0 / 6.0));

    const float2 scaled = float2(local.x * streak, local.y * thickness);
    const float2 rotated = float2(scaled.x * cos(angle) - scaled.y * sin(angle), scaled.x * sin(angle) + scaled.y * cos(angle));
    // Aspect correct so the streak keeps its angle on a wide screen
    const float2 offset = float2(rotated.x / gParams.glare.z, rotated.y);

    output.position = float4(uv * 2.0 - 1.0 + offset, 0.0, 1.0);
    output.local = local;
    output.color = color * gParams.tint.rgb * gParams.glare.x;
    return output;
}

[shader("fragment")]
float4 fragmentGlare(GlareVSOutput input) : SV_Target0
{
    // Bright along the length, fading out to the tips and across the width
    const float along = pow(saturate(1.0 - abs(input.local.x)), 2.0);
    const float across = exp(-3.0 * input.local.y * input.local.y);
    return float4(input.color * along * across, 1.0);
}

/// Colour ramp from warm in the middle to cool at the edge
float3 flareGradient(float2 uv)
{
    const float r = saturate(distance(uv, kCenter) * 2.0);
    return lerp(float3(1.0, 0.9, 0.75), float3(0.55, 0.75, 1.0), smoothstep(0.2, 1.0, r));
}

[shader("fragment")]
float4 fragmentMix(VSOutput input) : SV_Target0
{
    const float2 uv = input.uv;
    const float3 scene = gScene.Sample(uv).rgb;
    const float3 flares = gSource.Sample(uv).rgb;

    // The streaks are drawn as points so a small blur hides the tile grid
    const float2 pixel = gParams.texel.zw;
    float3 glare = gGlare.Sample(uv + float2(-1.0, -1.0) * pixel).rgb * 0.25;
    glare += gGlare.Sample(uv + float2(1.0, -1.0) * pixel).rgb * 0.25;
    glare += gGlare.Sample(uv + float2(-1.0, 1.0) * pixel).rgb * 0.25;
    glare += gGlare.Sample(uv + float2(1.0, 1.0) * pixel).rgb * 0.25;

    float3 streak = gStreak.Sample(uv).rgb * 2.0;
    const float streakLuma = dot(streak, float3(0.3333, 0.3333, 0.3333));
    streak = lerp(streak, streakLuma * kStreakTint * 1.6, gParams.streak.y) * gParams.streak.x;

    float3 total = ((flares + glare) * flareGradient(uv) + streak) * gParams.tint.rgb * gParams.tint.w;
    const float3 dirt = gParams.streak.w > 0.5 ? gDirtTexture.Sample(uv).rgb : 0;
    total *= 1.0 + gParams.streak.z * 4.0 * dirt;
    return float4(scene + total, 1.0);
}
