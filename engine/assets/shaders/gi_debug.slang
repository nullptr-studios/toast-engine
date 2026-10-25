// Red where a pixel finds no probe
import fullscreen;
import gi_common;

[[vk::binding(0, 0)]]
Sampler2D gSource;

[[vk::binding(1, 0)]]
Sampler2D gDepth;

[[vk::binding(2, 0)]]
StructuredBuffer<GiFrame> gFrame;

[[vk::binding(3, 0)]]
StructuredBuffer<uint> gTableKeys;

[[vk::binding(4, 0)]]
StructuredBuffer<uint> gTableValues;

[[vk::binding(5, 0)]]
StructuredBuffer<GiProbe> gPool;

[[vk::binding(6, 0)]]
Sampler2D gNormal;

[[vk::binding(7, 0)]]
StructuredBuffer<uint4> gDeposits;

[[vk::binding(8, 0)]]
StructuredBuffer<uint> gDepositSlots;

[[vk::binding(9, 0)]]
Sampler2D gIrradiance;

[[vk::binding(10, 0)]]
StructuredBuffer<GiHistory> gHistory;

/// y smoothed share the irradiance changed per frame
[[vk::binding(11, 0)]]
StructuredBuffer<float4> gChange;

struct GiDebugPush
{
    uint cascade;
    /// Mirrors renderer::GiSettings debug_mode
    uint mode;
};

[[vk::push_constant]]
GiDebugPush gPush;

static const uint kGiDebugCells = 0u;
static const uint kGiDebugRadiance = 1u;
static const uint kGiDebugCoverage = 2u;
static const uint kGiDebugIrradiance = 3u;
static const uint kGiDebugDepth = 4u;
static const uint kGiDebugChange = 5u;
static const uint kGiDebugSamples = 256u;

typedef FullscreenVSOutput VSOutput;

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    return fullscreenTriangle(vertexId);
}

uint giFind(GiFrame frame, uint cascade, int3 cell, uint lod)
{
    const GiCascadeLayout layout = frame.cascades[cascade];
    const uint key = giKey(cell, lod);
    uint slot = giHash(key) & layout.y;
    for (uint attempt = 0u; attempt < 32u; ++attempt)
    {
        const uint stored = gTableKeys[layout.x + slot];
        if (stored == key)
        {
            return gTableValues[layout.x + slot];
        }
        if (stored == kGiEmpty)
        {
            return kGiEmpty;
        }
        slot = (slot + 1u) & layout.y;
    }
    return kGiEmpty;
}

float3 giCellColour(int3 cell, uint lod)
{
    const uint hash = giHash(giKey(cell, lod) ^ 0x9e3779b9u);
    return float3(float(hash & 255u), float((hash >> 8) & 255u), float((hash >> 16) & 255u)) / 255.0 * 0.8 + 0.2;
}

/// Mean radiance of buckets with history or the share of facing buckets a ray reached this frame or their mean history
float3 giDepositColour(GiFrame frame, uint cascade, uint deposit, float3 normal)
{
    const uint4 layout = frame.deposits[cascade];
    const uint base = gDepositSlots[layout.x + deposit];
    const uint stride = max(layout.w / kGiDebugSamples, 1u);
    float3 radiance = float3(0.0, 0.0, 0.0);
    float filled = 0.0;
    float facing = 0.0;
    float covered = 0.0;
    float depth = 0.0;
    for (uint bucket = 0u; bucket < layout.w; bucket += stride)
    {
        const uint count = gDeposits[base + bucket].w & 0xffffu;
        float rays = 0.0;
        const float4 history = giHistoryBucket(gHistory[frame.frame.z * frame.pool.w + base + bucket], rays);
        if (rays > 0.0)
        {
            radiance += history.rgb;
            filled += 1.0;
        }
        if (dot(giDirectionOf(bucket, cascade), normal) > 0.25)
        {
            facing += 1.0;
            covered += count > 0u ? 1.0 : 0.0;
            depth += rays;
        }
    }
    if (gPush.mode == kGiDebugDepth)
    {
        // Red no history yellow about 8 rays green 64 and up
        const float share = saturate(log2(1.0 + (facing > 0.0 ? depth / facing : 0.0)) / 6.0);
        return share < 0.5 ? lerp(float3(1.0, 0.1, 0.1), float3(1.0, 0.9, 0.1), share * 2.0)
                           : lerp(float3(1.0, 0.9, 0.1), float3(0.1, 1.0, 0.2), share * 2.0 - 1.0);
    }
    if (gPush.mode == kGiDebugCoverage)
    {
        const float share = facing > 0.0 ? covered / facing : 0.0;
        return lerp(float3(1.0, 0.1, 0.1), float3(0.1, 1.0, 0.2), share);
    }
    const float3 mean = filled > 0.0 ? radiance / filled : float3(0.0, 0.0, 0.0);
    return mean / (1.0 + mean);
}

/// Same sparse trilinear c0 lookup lighting does
float3 giIrradianceAt(GiFrame frame, float3 world, float3 normal, uint lod, out bool found)
{
    const float spacing = giSpacing(frame, lod, 0u);
    const float3 coord = (world + normal * (kGiSurfaceBias * spacing)) / spacing - 0.5;
    const int3 base = int3(floor(coord));
    const float3 upper = coord - float3(base);
    float3 irradiance = float3(0.0, 0.0, 0.0);
    float total = 0.0;
    for (uint corner = 0u; corner < 8u; ++corner)
    {
        const uint slot = giFind(frame, 0u, base + giNeighbourOffset(corner), lod);
        if (slot == kGiEmpty || slot >= frame.cascades[0].w)
        {
            continue;
        }
        const GiProbe probe = gPool[frame.frame.z * frame.pool.x + frame.cascades[0].z + slot];
        if (probe.deposit == kGiEmpty)
        {
            continue;
        }
        const float weight = giNeighbourWeight(corner, upper);
        irradiance += weight * gIrradiance.SampleLevel(giTileUv(probe.deposit, normal), 0.0).rgb;
        total += weight;
    }
    found = total > 0.0;
    return found ? irradiance / total : irradiance;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    const int2 pixel = int2(input.position.xy);
    const float4 scene = gSource.Load(int3(pixel, 0));
    const GiFrame frame = gFrame[0];
    const uint cascade = min(gPush.cascade, frame.frame.y - 1u);
    const float depth = gDepth.Load(int3(pixel, 0)).r;
    if (giIsSky(depth))
    {
        return scene;
    }

    const float2 uv = (float2(pixel) + 0.5) / float2(frame.viewport.xy);
    const float3 world = giWorldAt(frame, uv, depth);
    const float3 normal = giSurfaceNormal(gNormal.Load(int3(pixel, 0)), world, frame.camera.xyz);
    bool overlap = false;
    const uint lod = giLod(frame, world, overlap);
    if (gPush.mode == kGiDebugChange)
    {
        // Black steady blue 1% yellow 5% red 15% and up of the irradiance changing every frame
        const float change = gChange[uint(pixel.y) * frame.viewport.x + uint(pixel.x)].y;
        const float3 heat = change < 0.01 ? lerp(float3(0.0, 0.0, 0.0), float3(0.1, 0.2, 1.0), change / 0.01)
                          : change < 0.05 ? lerp(float3(0.1, 0.2, 1.0), float3(1.0, 0.9, 0.1), (change - 0.01) / 0.04)
                                          : lerp(float3(1.0, 0.9, 0.1), float3(1.0, 0.1, 0.1), saturate((change - 0.05) / 0.1));
        return float4(heat, scene.a);
    }
    if (gPush.mode == kGiDebugIrradiance)
    {
        bool found = false;
        const float3 irradiance = giIrradianceAt(frame, world, normal, lod, found);
        return float4(found ? irradiance / (1.0 + irradiance) : float3(1.0, 0.0, 0.0), scene.a);
    }
    const float spacing = giSpacing(frame, lod, cascade);
    const int3 cell = giSurfaceCell(frame, world, normal, lod, cascade);

    const uint slot = giFind(frame, cascade, cell, lod);
    float3 colour = float3(1.0, 0.0, 0.0);
    if (slot != kGiEmpty && slot < frame.cascades[cascade].w)
    {
        const GiProbe probe = gPool[frame.frame.z * frame.pool.x + frame.cascades[cascade].z + slot];
        if (gPush.mode == kGiDebugCells)
        {
            colour = giCellColour(cell, lod) * (probe.lastSeen == frame.frame.x ? 1.0 : 0.35);
        }
        else if (probe.deposit != kGiEmpty)
        {
            colour = giDepositColour(frame, cascade, probe.deposit, normal);
        }
        else
        {
            // Carried or out of deposit room so no ray reached it this frame
            colour = float3(0.25, 0.0, 0.3);
        }
    }

    const float3 inside = frac((world + normal * (kGiSurfaceBias * giSpacing(frame, lod, 0u))) / spacing);
    const float edge = min(min(min(inside.x, 1.0 - inside.x), min(inside.y, 1.0 - inside.y)), min(inside.z, 1.0 - inside.z));
    colour *= edge < 0.06 ? 0.5 : 1.0;
    return float4(lerp(scene.rgb, colour, gPush.mode == kGiDebugCells ? 0.6 : 0.9), scene.a);
}
