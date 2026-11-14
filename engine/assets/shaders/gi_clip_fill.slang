// Set 1 is the voxel storage with the trace scene lists
import voxel_dda;
import gi_clip_common;

[[vk::binding(0, 0)]]
StructuredBuffer<GiClipFrame> gClipFrame;

/// Albedo rgb and opacity a
[[vk::binding(1, 0)]]
[[vk::image_format("rgba8")]]
RWTexture3D<float4> gMaterial;

[[vk::binding(2, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture3D<float4> gEmission;

/// xyz share of the cell cross section blocked along each axis and w 1 when anything is solid
[[vk::binding(3, 0)]]
[[vk::image_format("rgba8")]]
RWTexture3D<float4> gOpacity;

/// Bounce irradiance per giClipFace which only the scroll clears here
[[vk::binding(4, 0)]]
[[vk::image_format("r32ui")]]
RWTexture3D<uint> gAmbient[kGiClipFaces];

// Mirrors scene_trace.slang which is not imported since it pulls in lighting
[[vk::binding(10, 1)]]
StructuredBuffer<VoxelInstance> gTraceVolumes;
/// xyz centre w radius
[[vk::binding(11, 1)]]
StructuredBuffer<float4> gTraceBounds;
[[vk::binding(12, 1)]]
StructuredBuffer<uint> gTraceGrid;

static const uint kTraceFarCount = 6u;
static const uint kTraceFarOffset = 8u;
static const uint kTraceEntriesOffset = 10u;
static const uint kTraceHeaderWords = 16u;

struct GiClipFillPush
{
    /// xyz first global cell w level
    int4 boxMin;
    /// w 1 for cells that scrolled in
    int4 boxSize;
};

[[vk::push_constant]]
GiClipFillPush gPush;

static const uint kClipSide = 4u;
static const uint kClipSamples = kClipSide * kClipSide * kClipSide;

struct ClipCell
{
    float3 albedo;
    float albedoCount;
    float3 emission;
    /// One bit per sample at x + 4 y + 16 z with z 0 and 1 in x
    uint2 solid;
};

bool clipSolidBit(ClipCell cell, uint index)
{
    return (((index < 32u ? cell.solid.x : cell.solid.y) >> (index & 31u)) & 1u) != 0u;
}

void clipSample(uint v, float3 world, float gap, uint index, inout ClipCell cell)
{
    if (clipSolidBit(cell, index))
    {
        return;
    }
    const VoxelInstance volume = gTraceVolumes[v];
    const VolumeRecord record = gVolumeRecords[volume.recordIndex];
    // Pyramid block at least as wide as the sample gap in voxels so nothing thinner slips between samples
    const float gapVoxels = gap * length(volume.worldToVoxel[0].xyz);
    const uint pyramid = uint(clamp(ceil(log2(max(gapVoxels, 1.0)) - 0.01), 0.0, float(kLevelCoarse)));
    const int3 voxel = int3(floor(mul(volume.worldToVoxel, float4(world, 1.0)).xyz));
    // occupiedAtLevel only bounds the coarse cell and a coarse cell can reach past the bricks
    const int3 dims = int3(record.brickDimsX, record.brickDimsY, record.brickDimsZ) * int(kBrickDim);
    if (any(voxel < 0) || any(voxel >= dims) || !occupiedAtLevel(record, voxel, pyramid, true))
    {
        return;
    }
    if (index < 32u)
    {
        cell.solid.x |= 1u << index;
    }
    else
    {
        cell.solid.y |= 1u << (index - 32u);
    }

    uint material = voxelMaterialAt(record, voxel);
    // A solid block of the pyramid can hold an empty voxel right at the sample
    const int shift = int(min(pyramid, 3u));
    const int3 base = (voxel >> shift) << shift;
    const int last = (1 << shift) - 1;
    for (uint corner = 0u; corner < 8u && material == 0u && shift > 0; ++corner)
    {
        material = voxelMaterialAt(record, base + int3(corner & 1u, (corner >> 1) & 1u, corner >> 2) * last);
    }
    if (material == 0u)
    {
        return;
    }
    const PaletteSample palette = samplePalette(record, material);
    cell.albedo += palette.albedo;
    cell.albedoCount += 1.0;
    cell.emission += palette.albedo * palette.emissive;
}

/// 16 bits of one z layer at x + 4 y
uint clipLayer(ClipCell cell, uint z)
{
    return ((z < 2u ? cell.solid.x : cell.solid.y) >> ((z & 1u) * 16u)) & 0xFFFFu;
}

float3 clipBlocked(ClipCell cell)
{
    float3 blocked = float3(0.0, 0.0, 0.0);
    uint any = 0u;
    for (uint z = 0u; z < kClipSide; ++z)
    {
        const uint layer = clipLayer(cell, z);
        any |= layer;
        for (uint i = 0u; i < kClipSide; ++i)
        {
            blocked.x += ((layer >> (i * 4u)) & 0xFu) != 0u ? 1.0 : 0.0;
            blocked.y += (layer & (0x1111u << i)) != 0u ? 1.0 : 0.0;
        }
    }
    blocked.z = float(countbits(any));
    return blocked / float(kClipSide * kClipSide);
}

float3 clipSamplePoint(float3 low, float cellSize, uint index)
{
    return low + (float3(index & 3u, (index >> 2) & 3u, index >> 4) + 0.5) * (cellSize / float(kClipSide));
}

[shader("compute")]
[numthreads(4, 4, 4)]
void fillMain(uint3 id : SV_DispatchThreadID)
{
    if (any(int3(id) >= gPush.boxSize.xyz))
    {
        return;
    }
    const GiClipFrame frame = gClipFrame[0];
    const uint level = uint(gPush.boxMin.w);
    const float cellSize = frame.levels[level].w;
    const int3 global = gPush.boxMin.xyz + int3(id);
    const float3 low = float3(global) * cellSize;
    const float3 centre = low + 0.5 * cellSize;
    const float gap = cellSize / float(kClipSide);

    ClipCell cell;
    cell.albedo = float3(0.0, 0.0, 0.0);
    cell.albedoCount = 0.0;
    cell.emission = float3(0.0, 0.0, 0.0);
    cell.solid = uint2(0u, 0u);

    const uint farCount = gTraceGrid[kTraceFarCount];
    const uint farBase = gTraceGrid[kTraceFarOffset];
    for (uint f = 0u; f < farCount; ++f)
    {
        const uint v = gTraceGrid[farBase + f];
        const float4 bounds = gTraceBounds[v];
        if (distance(centre, bounds.xyz) > bounds.w + cellSize * 0.8661)
        {
            continue;
        }
        for (uint s = 0u; s < kClipSamples; ++s)
        {
            clipSample(v, clipSamplePoint(low, cellSize, s), gap, s, cell);
        }
    }

    const float3 gridOrigin = float3(asfloat(gTraceGrid[0]), asfloat(gTraceGrid[1]), asfloat(gTraceGrid[2]));
    const float gridCell = asfloat(gTraceGrid[3]);
    const int gridDim = int(gTraceGrid[4]);
    const uint entriesBase = gTraceGrid[kTraceEntriesOffset];
    for (uint s = 0u; s < kClipSamples; ++s)
    {
        const float3 point = clipSamplePoint(low, cellSize, s);
        const int3 g = int3(floor((point - gridOrigin) / gridCell));
        if (any(g < 0) || any(g >= gridDim))
        {
            continue;
        }
        const uint index = uint(g.x + g.y * gridDim + g.z * gridDim * gridDim);
        const uint first = gTraceGrid[kTraceHeaderWords + 2u * index];
        const uint count = gTraceGrid[kTraceHeaderWords + 2u * index + 1u];
        for (uint e = 0u; e < count; ++e)
        {
            const uint v = gTraceGrid[entriesBase + first + e];
            const float4 bounds = gTraceBounds[v];
            if (distance(point, bounds.xyz) > bounds.w)
            {
                continue;
            }
            clipSample(v, point, gap, s, cell);
        }
    }

    const int3 texel = giClipWrap(global, int(frame.size.x));
    const float3 albedo = cell.albedoCount > 0.0 ? cell.albedo / cell.albedoCount : float3(0.5, 0.5, 0.5);
    const bool solid = any(cell.solid != 0u);
    gMaterial[texel] = float4(albedo, solid ? 1.0 : 0.0);
    // Over the solid samples like the albedo
    gEmission[texel] = float4(cell.emission / max(cell.albedoCount, 1.0), 0.0);
    // A slab seen edge on blocks only its share so cones over a floor are not swallowed by it
    if (!giClipInner(frame, level, global))
    {
        gOpacity[texel] = float4(clipBlocked(cell), solid ? 1.0 : 0.0);
    }
    // Cells that scrolled in hold the bounce of whatever wrapped there before
    if (gPush.boxSize.w != 0)
    {
        for (uint face = 0u; face < kGiClipFaces; ++face)
        {
            gAmbient[face][texel] = 0u;
        }
    }
}
