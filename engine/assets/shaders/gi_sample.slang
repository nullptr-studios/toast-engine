// Set 0 written by SceneDescriptorSets from GiPass
import gi_common;

/// Copied by GiPass after its merge so this is the frame the tables belong to and frame.x 0 means no GI yet
[[vk::binding(17, 0)]]
StructuredBuffer<GiFrame> gGiFrame;

[[vk::binding(18, 0)]]
StructuredBuffer<uint> gGiTableKeys;

[[vk::binding(19, 0)]]
StructuredBuffer<uint> gGiTableValues;

[[vk::binding(20, 0)]]
StructuredBuffer<GiProbe> gGiPool;

/// c0 octahedral tiles holding E / pi
[[vk::binding(21, 0)]]
Sampler2D gGiIrradiance;

static const uint kRcMaxProbes = 32u;

/// c0 tile per corner around @p base or kGiEmpty with each stage batched over all eight
void rcFindTiles(GiFrame frame, int3 base, uint lod, out uint tiles[8])
{
    const GiCascadeLayout layout = frame.cascades[0];
    uint keys[8];
    uint slots[8];
    uint stored[8];
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner)
    {
        keys[corner] = giKey(base + giNeighbourOffset(corner), lod);
        slots[corner] = giHash(keys[corner]) & layout.y;
        stored[corner] = gGiTableKeys[layout.x + slots[corner]];
    }
    // Probing past the first slot is rare with the tables at most half full
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner)
    {
        for (uint attempt = 1u; attempt < kRcMaxProbes && stored[corner] != keys[corner] && stored[corner] != kGiEmpty; ++attempt)
        {
            slots[corner] = (slots[corner] + 1u) & layout.y;
            stored[corner] = gGiTableKeys[layout.x + slots[corner]];
        }
    }
    uint values[8];
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner)
    {
        values[corner] = gGiTableValues[layout.x + slots[corner]];
    }
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner)
    {
        const bool found = stored[corner] == keys[corner] && values[corner] < layout.w;
        const uint tile = gGiPool[frame.frame.z * frame.pool.x + layout.z + (found ? values[corner] : 0u)].deposit;
        tiles[corner] = found ? tile : kGiEmpty;
    }
}

/// Corner tiles around the biased point and its position between them
void rcCornersAtLod(GiFrame frame, float3 worldPosition, float3 normal, uint lod, out float3 upper, out uint tiles[8])
{
    const float spacing = giSpacing(frame, lod, 0u);
    const float3 coord = (worldPosition + normal * (kGiSurfaceBias * spacing)) / spacing - 0.5;
    const int3 base = int3(floor(coord));
    upper = coord - float3(base);
    rcFindTiles(frame, base, lod, tiles);
}

/// Weight rcIrradianceAtLod would sum without its irradiance fetches
float rcCoverageAtLod(GiFrame frame, float3 worldPosition, float3 normal, uint lod)
{
    float3 upper;
    uint tiles[8];
    rcCornersAtLod(frame, worldPosition, normal, lod, upper, tiles);
    float total = 0.0;
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner)
    {
        total += tiles[corner] != kGiEmpty ? giNeighbourWeight(corner, upper) : 0.0;
    }
    return total;
}

float3 rcIrradianceAtLod(GiFrame frame, float3 worldPosition, float3 normal, uint lod, out float total)
{
    float3 upper;
    uint tiles[8];
    rcCornersAtLod(frame, worldPosition, normal, lod, upper, tiles);

    // Every tile is read at the same normal so the octahedral texel is shared
    const float2 texel = 1.0 + giOctEncode(normal) * float(kGiTileInterior);
    float3 irradiance = float3(0.0, 0.0, 0.0);
    total = 0.0;
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner)
    {
        // A missing probe samples tile 0 at no weight so the eight fetches issue together
        const bool found = tiles[corner] != kGiEmpty;
        const float weight = found ? giNeighbourWeight(corner, upper) : 0.0;
        const float2 uv = (float2(giTileOrigin(found ? tiles[corner] : 0u)) + texel) / kGiAtlasSize;
        irradiance += weight * gGiIrradiance.SampleLevel(uv, 0.0).rgb;
        total += weight;
    }
    return total > 0.0 ? irradiance / total : irradiance;
}

/// Over pi like gIrradianceMap and false where no probe covers the point
bool sampleRadianceCascades(float3 worldPosition, float3 normal, out float3 irradiance)
{
    irradiance = float3(0.0, 0.0, 0.0);
    const GiFrame frame = gGiFrame[0];
    if (frame.frame.x == 0u)
    {
        return false;
    }
    bool overlap = false;
    const uint lod = giLod(frame, worldPosition, overlap);
    float nearTotal = 0.0;
    const float3 near = rcIrradianceAtLod(frame, worldPosition, normal, lod, nearTotal);

    float blend = 0.0;
    float farTotal = 0.0;
    float3 far = float3(0.0, 0.0, 0.0);
    if (overlap)
    {
        const float3 offset = abs(worldPosition - frame.camera.xyz);
        const float edge = frame.lod.x * float(1u << min(lod + 1u, 30u));
        blend = saturate((max(offset.x, max(offset.y, offset.z)) - frame.lod.y * edge) / ((1.0 - frame.lod.y) * edge));
        far = rcIrradianceAtLod(frame, worldPosition, normal, lod + 1u, farTotal);
    }
    if (nearTotal <= 0.0 && farTotal <= 0.0)
    {
        return false;
    }
    irradiance = nearTotal <= 0.0 ? far : (farTotal <= 0.0 ? near : lerp(near, far, blend));
    return true;
}

/// Whether sampleRadianceCascades finds a probe at the point
bool radianceCascadesCover(float3 worldPosition, float3 normal)
{
    const GiFrame frame = gGiFrame[0];
    if (frame.frame.x == 0u)
    {
        return false;
    }
    bool overlap = false;
    const uint lod = giLod(frame, worldPosition, overlap);
    return rcCoverageAtLod(frame, worldPosition, normal, lod) > 0.0 ||
           (overlap && rcCoverageAtLod(frame, worldPosition, normal, lod + 1u) > 0.0);
}

/// End of the c0 interval at a point so short contact rays can add what the probe spacing blurs
float radianceCascadesContactReach(float3 worldPosition)
{
    const GiFrame frame = gGiFrame[0];
    bool overlap = false;
    return frame.rays.x * giSpacing(frame, giLod(frame, worldPosition, overlap), 0u);
}
