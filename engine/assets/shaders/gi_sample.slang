// Set 0 written by SceneDescriptorSets from GiPass
import gi_common;
import gi_clip_common;

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

/// Published by GiClipmapPass after its inject and size.z 0 means no clipmap GI
[[vk::binding(24, 0)]]
StructuredBuffer<GiClipFrame> gGiClipFrame;

/// Face radiance at level * kGiClipFaces + face
[[vk::binding(25, 0)]]
Sampler3D gGiClipRadiance[kGiClipMaxLevels * kGiClipFaces];

/// xyz opacity along each axis and w the solid share
[[vk::binding(26, 0)]]
Sampler3D gGiClipOpacity[kGiClipMaxLevels];

/// Bounce irradiance over pi each face gathered at level * kGiClipFaces + face
[[vk::binding(27, 0)]]
Sampler3D gGiClipAmbient[kGiClipMaxLevels * kGiClipFaces];

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

/// Diameter per meter of a 60 degree cone
static const float kGiClipConeSpread = 1.1547;
static const uint kGiClipConeSteps = 10u;
/// 0 since finer levels leave blind rings between cones that show as halos
static const float kGiClipConeBias = 0.0;
/// Cells of level 0 the cones start off the surface so its own cells do not shade it
static const float kGiClipConeLift = 1.5;
static const float kGiClipFaceWeight = 1e-4;

/// Premultiplied radiance of the faces looking back at the ray and opacity along it
/// @p lod above 0 reads the mips of the last level which stand in for coarser levels over the same box
float4 giClipSample(GiClipFrame frame, uint level, float lod, float3 world, float3 direction)
{
    // The repeat sampler does the toroidal wrap
    const float3 uvw = world / (frame.levels[level].w * float(frame.size.x));
    const float3 blocked = gGiClipOpacity[NonUniformResourceIndex(level)].SampleLevel(uvw, lod).xyz;
    const float3 weight = direction * direction;
    const float opacity = dot(weight, blocked);
    if (opacity <= 0.0)
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    const uint base = level * kGiClipFaces;
    float3 radiance = float3(0.0, 0.0, 0.0);
    [unroll]
    for (uint axis = 0u; axis < 3u; ++axis)
    {
        if (weight[axis] > kGiClipFaceWeight)
        {
            const uint face = base + axis * 2u + (direction[axis] > 0.0 ? 1u : 0u);
            radiance += weight[axis] * gGiClipRadiance[NonUniformResourceIndex(face)].SampleLevel(uvw, lod).rgb;
        }
    }
    // Each face is stored times its own coverage so this is already premultiplied
    return float4(radiance, opacity);
}

/// Weight and place of one of the eight filtered reads of a quadratic B-spline over the 27 cells around a point
float4 giClipSmoothTap(float3 cell, float3 f, float cells, uint tap)
{
    const float3 after = 0.5 * (0.5 + f) * (0.5 + f);
    const float3 near = 1.0 - after;
    const float3 before = 0.5 * (0.5 - f) * (0.5 - f);
    const bool3 far = bool3((tap & 1u) != 0u, (tap & 2u) != 0u, (tap & 4u) != 0u);
    const float3 share = select(far, after, near);
    const float3 at = select(far, cell + 1.5, cell - 0.5 + (near - before) / near);
    return float4(at / cells, share.x * share.y * share.z);
}

/// Trilinear draws cells as creased pyramids so this rounds them at 8 reads per texture
/// The taps are worked out again for each read since holding all eight costs registers every pixel pays for
float4 giClipSampleSmooth(GiClipFrame frame, float3 world, float3 direction)
{
    const float cells = float(frame.size.x);
    const float3 p = world / frame.levels[0].w;
    const float3 cell = floor(p);
    // Offset from the centre of the cell holding the point in -0.5 to 0.5
    const float3 f = p - cell - 0.5;

    float3 blocked = float3(0.0, 0.0, 0.0);
    for (uint tap = 0u; tap < 8u; ++tap)
    {
        const float4 read = giClipSmoothTap(cell, f, cells, tap);
        blocked += read.w * gGiClipOpacity[0].SampleLevel(read.xyz, 0.0).xyz;
    }
    const float3 weight = direction * direction;
    const float opacity = dot(weight, blocked);
    if (opacity <= 0.0)
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    const uint3 face = uint3(direction.x > 0.0 ? 1u : 0u, direction.y > 0.0 ? 3u : 2u, direction.z > 0.0 ? 5u : 4u);
    float3 radiance = float3(0.0, 0.0, 0.0);
    for (uint tap = 0u; tap < 8u; ++tap)
    {
        const float4 read = giClipSmoothTap(cell, f, cells, tap);
        [unroll]
        for (uint axis = 0u; axis < 3u; ++axis)
        {
            if (weight[axis] > kGiClipFaceWeight)
            {
                radiance += read.w * weight[axis] *
                            gGiClipRadiance[NonUniformResourceIndex(face[axis])].SampleLevel(read.xyz, 0.0).rgb;
            }
        }
    }
    return float4(radiance, opacity);
}

float giClipSolidShare(GiClipFrame frame, uint level, float3 world)
{
    const float3 uvw = world / (frame.levels[level].w * float(frame.size.x));
    return gGiClipOpacity[NonUniformResourceIndex(level)].SampleLevel(uvw, 0.0).w;
}

/// Radiance rgb and coverage a starting @p start meters along the cone and reading no level finer than @p base
/// Smooth above 0 reads level 0 through giClipSampleSmooth and is a compile time choice so shaders that only
/// take the plain cone do not carry the smooth filter
float4 giClipCone<let Smooth : int>(GiClipFrame frame, float3 origin, float3 direction, float start, float base)
{
    const float cell = frame.levels[0].w;
    const float last = float(frame.size.y - 1u);
    float t = start;
    float4 sum = float4(0.0, 0.0, 0.0, 0.0);
    for (uint step = 0u; step < kGiClipConeSteps && sum.a < 0.98; ++step)
    {
        const float3 point = origin + direction * t;
        const float held = giClipLevelSoft(frame, point);
        if (held >= float(kGiClipMaxLevels))
        {
            break;
        }
        const float diameter = max(kGiClipConeSpread * t, cell);
        // Cones read cells as wide as themselves or leave blind rings so past the last level they climb its mips
        const float wanted = clamp(log2(diameter / cell) - kGiClipConeBias, max(min(held, last), base), last + float(frame.size.w));
        float4 sample;
        if (wanted > last)
        {
            sample = giClipSample(frame, uint(last), wanted - last, point, direction);
        }
        else
        {
            const uint fine = uint(floor(wanted));
            const uint coarse = min(fine + 1u, uint(last));
            const float blend = wanted - float(fine);
            if (Smooth > 0 && fine == 0u && coarse != fine)
            {
                // Level 1 reaches past the 27 cells so where it holds nothing they are empty and one read settles it
                sample = float4(0.0, 0.0, 0.0, 0.0);
                if (giClipSolidShare(frame, coarse, point) > 0.0)
                {
                    sample = giClipSampleSmooth(frame, point, direction);
                    if (blend > 0.0)
                    {
                        sample = lerp(sample, giClipSample(frame, coarse, 0.0, point, direction), blend);
                    }
                }
            }
            else if (blend > 0.0 && coarse != fine)
            {
                // Coarse is built from fine so where it is empty so is fine and the step costs one read
                sample = giClipSample(frame, coarse, 0.0, point, direction);
                if (sample.a > 0.0)
                {
                    sample = lerp(giClipSample(frame, fine, 0.0, point, direction), sample, blend);
                }
            }
            else
            {
                sample = giClipSample(frame, fine, 0.0, point, direction);
            }
        }
        sum += (1.0 - sum.a) * sample;
        // One cell of the level read a step so each cell is composited once with no opacity correction
        t += max(diameter, cell * exp2(wanted));
    }
    return sum;
}

float giClipCellAt(GiClipFrame frame, float3 worldPosition)
{
    return frame.levels[0].w * exp2(min(giClipLevelSoft(frame, worldPosition), float(frame.size.y - 1u)));
}

/// xyz irradiance over pi and w the share of the hemisphere the cones left open
float4 giClipGather(GiClipFrame frame, float3 worldPosition, float3 normal, float turn, float base)
{
    // Coarser levels thicken every surface so the cones start further off it there
    const float cell = frame.levels[0].w * exp2(base);
    const float3 origin = worldPosition + normal * (kGiClipConeLift * cell);
    const float side = normal.z >= 0.0 ? 1.0 : -1.0;
    const float a = -1.0 / (side + normal.z);
    const float b = normal.x * normal.y * a;
    const float3 tangent = float3(1.0 + side * normal.x * normal.x * a, side * b, -side * normal.x);
    const float3 bitangent = float3(b, side + normal.y * normal.y * a, -normal.y);

    float4 cone = giClipCone<1>(frame, origin, normal, 0.0, base);
    float4 gathered = 0.25 * float4(cone.rgb, 1.0 - cone.a);
    for (uint i = 0u; i < 5u; ++i)
    {
        const float angle = (float(i) + turn) * 1.25663706;
        const float3 direction = normal * 0.5 + (tangent * cos(angle) + bitangent * sin(angle)) * 0.8660254;
        cone = giClipCone<1>(frame, origin, direction, 0.0, base);
        gathered += 0.15 * float4(cone.rgb, 1.0 - cone.a);
    }
    return gathered;
}

/// @p turn in 0 to 1 spins the side cones by up to the fifth of a turn between two of them
/// Nothing here is random and lit pixels pass 0 since any per frame noise flickers around bright lights
bool sampleGiClipmap(float3 worldPosition, float3 normal, float turn, out float3 irradiance, out float sky)
{
    irradiance = float3(0.0, 0.0, 0.0);
    sky = 0.0;
    const GiClipFrame frame = gGiClipFrame[0];
    if (frame.size.z == 0u)
    {
        return false;
    }
    const float soft = giClipLevelSoft(frame, worldPosition);
    if (soft >= float(kGiClipMaxLevels))
    {
        return false;
    }
    const float last = float(frame.size.y - 1u);
    const float base = floor(min(soft, last));
    float4 gathered = giClipGather(frame, worldPosition, normal, turn, base);
    // Near a level edge two gathers cross fade since sliding one start between levels draws strips
    const float fade = min(soft, last) - base;
    if (fade > 0.0)
    {
        gathered = lerp(gathered, giClipGather(frame, worldPosition, normal, turn, base + 1.0), fade);
    }
    irradiance = gathered.rgb;
    sky = gathered.a;
    return true;
}

bool sampleGiClipmapCheap(float3 worldPosition, float3 normal, out float3 irradiance, out float sky)
{
    irradiance = float3(0.0, 0.0, 0.0);
    sky = 0.0;
    const GiClipFrame frame = gGiClipFrame[0];
    if (frame.size.z == 0u)
    {
        return false;
    }
    const float soft = giClipLevelSoft(frame, worldPosition);
    if (soft >= float(kGiClipMaxLevels))
    {
        return false;
    }
    // The coarser level of a fade band alone so the band costs one cone too
    const float base = ceil(min(soft, float(frame.size.y - 1u)));
    const float3 origin = worldPosition + normal * (kGiClipConeLift * frame.levels[0].w * exp2(base));
    const float4 cone = giClipCone<0>(frame, origin, normal, 0.0, base);
    irradiance = cone.rgb;
    sky = 1.0 - cone.a;
    return true;
}

bool giIrradianceCheap(float3 worldPosition, float3 normal, out float3 irradiance, out float sky)
{
    if (sampleGiClipmapCheap(worldPosition, normal, irradiance, sky))
    {
        return true;
    }
    sky = 0.0;
    return sampleRadianceCascades(worldPosition, normal, irradiance);
}

/// The clipmap when it is published else the cascades whose @p sky is 0 since they hold the sky
bool giIrradiance(float3 worldPosition, float3 normal, float turn, out float3 irradiance, out float sky)
{
    if (sampleGiClipmap(worldPosition, normal, turn, irradiance, sky))
    {
        return true;
    }
    sky = 0.0;
    return sampleRadianceCascades(worldPosition, normal, irradiance);
}

/// Irradiance over pi the clipmap already gathered at the faces around a surface point and 0 share off any surface
float4 giClipAmbientAt(GiClipFrame frame, uint level, float3 worldPosition, float3 normal)
{
    const float3 uvw = worldPosition / (frame.levels[level].w * float(frame.size.x));
    const float share = gGiClipOpacity[NonUniformResourceIndex(level)].SampleLevel(uvw, 0.0).w;
    const float3 weight = normal * normal;
    const uint base = level * kGiClipFaces;
    float3 irradiance = float3(0.0, 0.0, 0.0);
    [unroll]
    for (uint axis = 0u; axis < 3u; ++axis)
    {
        const uint face = base + axis * 2u + (normal[axis] > 0.0 ? 0u : 1u);
        irradiance += weight[axis] * gGiClipAmbient[NonUniformResourceIndex(face)].SampleLevel(uvw, 0.0).rgb;
    }
    return float4(irradiance, share);
}

/// Empty cells hold nothing so the filtered value is divided by the solid share around the point
bool sampleGiClipAmbient(float3 worldPosition, float3 normal, out float3 irradiance)
{
    irradiance = float3(0.0, 0.0, 0.0);
    const GiClipFrame frame = gGiClipFrame[0];
    if (frame.size.z == 0u)
    {
        return false;
    }
    const float soft = giClipLevelSoft(frame, worldPosition);
    if (soft >= float(kGiClipMaxLevels))
    {
        return false;
    }
    const float last = float(frame.size.y - 1u);
    const float level = min(soft, last);
    const uint fine = uint(floor(level));
    float4 ambient = giClipAmbientAt(frame, fine, worldPosition, normal);
    if (level > float(fine))
    {
        ambient = lerp(ambient, giClipAmbientAt(frame, fine + 1u, worldPosition, normal), level - float(fine));
    }
    if (ambient.a < 0.05)
    {
        return false;
    }
    irradiance = ambient.rgb / ambient.a;
    return true;
}

bool giHitIrradiance(float3 worldPosition, float3 normal, out float3 irradiance)
{
    if (sampleGiClipAmbient(worldPosition, normal, irradiance))
    {
        return true;
    }
    return sampleRadianceCascades(worldPosition, normal, irradiance);
}

bool giClipCovers(float3 worldPosition)
{
    const GiClipFrame frame = gGiClipFrame[0];
    return frame.size.z != 0u && giClipLevelSoft(frame, worldPosition) < float(kGiClipMaxLevels);
}

bool giCover(float3 worldPosition, float3 normal)
{
    return giClipCovers(worldPosition) || radianceCascadesCover(worldPosition, normal);
}

float giContactReach(float3 worldPosition)
{
    if (giClipCovers(worldPosition))
    {
        return kGiClipConeLift * giClipCellAt(gGiClipFrame[0], worldPosition);
    }
    return radianceCascadesContactReach(worldPosition);
}

/// Each shader calls the one it needs so it compiles only that path

float4 giFull(float3 worldPosition, float3 normal)
{
    float3 irradiance;
    float sky;
    return giIrradiance(worldPosition, normal, 0.0, irradiance, sky) ? float4(irradiance, sky) : float4(0.0, 0.0, 0.0, -1.0);
}

float4 giCheap(float3 worldPosition, float3 normal)
{
    float3 irradiance;
    float sky;
    return giIrradianceCheap(worldPosition, normal, irradiance, sky) ? float4(irradiance, sky) : float4(0.0, 0.0, 0.0, -1.0);
}

float4 giCascades(float3 worldPosition, float3 normal)
{
    float3 irradiance;
    return sampleRadianceCascades(worldPosition, normal, irradiance) ? float4(irradiance, 0.0) : float4(0.0, 0.0, 0.0, -1.0);
}
