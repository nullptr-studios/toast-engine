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

uint rcFindTile(GiFrame frame, int3 cell, uint lod)
{
    const GiCascadeLayout layout = frame.cascades[0];
    const uint key = giKey(cell, lod);
    uint slot = giHash(key) & layout.y;
    for (uint attempt = 0u; attempt < kRcMaxProbes; ++attempt)
    {
        const uint stored = gGiTableKeys[layout.x + slot];
        if (stored == key)
        {
            const uint value = gGiTableValues[layout.x + slot];
            return value < layout.w ? gGiPool[frame.frame.z * frame.pool.x + layout.z + value].deposit : kGiEmpty;
        }
        if (stored == kGiEmpty)
        {
            return kGiEmpty;
        }
        slot = (slot + 1u) & layout.y;
    }
    return kGiEmpty;
}

float3 rcIrradianceAtLod(GiFrame frame, float3 worldPosition, float3 normal, uint lod, out float total)
{
    const float spacing = giSpacing(frame, lod, 0u);
    const float3 coord = (worldPosition + normal * (kGiSurfaceBias * spacing)) / spacing - 0.5;
    const int3 base = int3(floor(coord));
    const float3 upper = coord - float3(base);
    float3 irradiance = float3(0.0, 0.0, 0.0);
    total = 0.0;
    for (uint corner = 0u; corner < 8u; ++corner)
    {
        const uint tile = rcFindTile(frame, base + giNeighbourOffset(corner), lod);
        if (tile == kGiEmpty)
        {
            continue;
        }
        const float weight = giNeighbourWeight(corner, upper);
        irradiance += weight * gGiIrradiance.SampleLevel(giTileUv(tile, normal), 0.0).rgb;
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

/// End of the c0 interval at a point so short contact rays can add what the probe spacing blurs
float radianceCascadesContactReach(float3 worldPosition)
{
    const GiFrame frame = gGiFrame[0];
    bool overlap = false;
    return frame.rays.x * giSpacing(frame, giLod(frame, worldPosition, overlap), 0u);
}
