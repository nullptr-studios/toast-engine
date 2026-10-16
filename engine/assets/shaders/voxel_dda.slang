// Twin of toast/voxel/ray_march.cpp with layout from toast/voxel/gpu_layout.hpp so change them together

/// Mirrors voxel::gpu::VolumeRecord
struct VolumeRecord
{
    uint brickDimsX;
    uint brickDimsY;
    uint brickDimsZ;
    uint gridOffset;
    uint coarseDimsX;
    uint coarseDimsY;
    uint coarseDimsZ;
    uint coarseOffset;
    uint paletteOffset;
    float maxEmissive;
};

/// Mirrors renderer::VoxelInstanceGpu
struct VoxelInstance
{
    /// Voxel (x y z) occupies [x x + 1] in this space
    float4x4 voxelToWorld;
    float4x4 worldToVoxel;
    float4x4 previousVoxelToWorld;
    uint recordIndex;
    uint highlightRecord; /// this is for selection tint
    /// Bit 0 no real previous frame and bit 1 a mirrored transform
    uint flags;
    /// First global coarse cell of this instance in the cell cull dispatch
    uint cellBase;
};

static const uint kInstanceNoMotion = 1u;
static const uint kInstanceMirrored = 2u;

[[vk::binding(0, 1)]]
StructuredBuffer<uint> gBrickMaterials;
[[vk::binding(1, 1)]]
StructuredBuffer<uint> gBrickOccupancy;
[[vk::binding(2, 1)]]
StructuredBuffer<uint> gVolumeGrids;
[[vk::binding(3, 1)]]
StructuredBuffer<uint> gVolumeCoarse;
[[vk::binding(4, 1)]]
StructuredBuffer<uint> gPalettes;
[[vk::binding(5, 1)]]
StructuredBuffer<VolumeRecord> gVolumeRecords;
[[vk::binding(6, 1)]]
StructuredBuffer<VoxelInstance> gVoxelInstances;

static const uint kBrickDim = 8;
static const int kCoarseVoxels = 32;    // gpu::k_coarse_bricks * k_brick_dim
static const uint kCoarseBricks = 4u;    // gpu::k_coarse_bricks
static const uint kMaterialWordsPerBrick = 128;
static const uint kOccupancyWordsPerBrick = 16;
static const uint kMaxMarchSteps = 1024;
static const float kMarchTieEpsilon = 1e-6;    // Matches k_march_tie_epsilon
static const float kFloatMax = 3.402823e38;

// Counter clockwise from outside
static const float3 kBoxCorners[8] = {
    float3(0.0, 0.0, 0.0), float3(1.0, 0.0, 0.0), float3(1.0, 1.0, 0.0), float3(0.0, 1.0, 0.0),
    float3(0.0, 0.0, 1.0), float3(1.0, 0.0, 1.0), float3(1.0, 1.0, 1.0), float3(0.0, 1.0, 1.0),
};

static const uint kBoxIndices[36] = {
    0, 2, 1, 0, 3, 2,    // -Z
    4, 5, 6, 4, 6, 7,    // +Z
    0, 1, 5, 0, 5, 4,    // -Y
    3, 7, 6, 3, 6, 2,    // +Y
    0, 4, 7, 0, 7, 3,    // -X
    1, 2, 6, 1, 6, 5,    // +X
};

/// Cell args word layout mirrored by renderer::VoxelPass
/// Draw commands per box bucket with bucket 0 holding the camera and then the late list, offsets and cursors per list,
/// the mesh dispatch, the late cull dispatch, the late mesh dispatch, the quad draw, the late quad draw and the occlusion
/// stats
static const uint kCellBucketCount = 17;
/// Late occlusion survivors stored after every other list
static const uint kLateList = kCellBucketCount;
/// Lists with a draw command at word list * 4
static const uint kCellDrawCount = kCellBucketCount + 1;
/// Adjacent distance bands so the near region reads front to back
static const uint kNearList = kCellDrawCount;
static const uint kNearBandCount = 4;
/// Cells behind the previous pyramid that the late phase tests again
static const uint kRetestList = kNearList + kNearBandCount;
/// Late near survivors filling down from the end of the late region so both share the retest count
static const uint kLateNearList = kRetestList + 1;
static const uint kCellListCount = kLateNearList + 1;
static const uint kCellOffsetsWord = kCellDrawCount * 4;
static const uint kCellCursorsWord = kCellOffsetsWord + kCellListCount;
static const uint kNearDispatchWord = kCellCursorsWord + kCellListCount;
static const uint kRetestDispatchWord = kNearDispatchWord + 3;
static const uint kLateMeshDispatchWord = kRetestDispatchWord + 3;
static const uint kQuadDrawWord = kLateMeshDispatchWord + 3;
/// First vertex is where the early quads end
static const uint kLateQuadDrawWord = kQuadDrawWord + 4;
/// Task dispatches of the near and late near lists with one group per cell in x and kTaskGroupsPerCell in y
static const uint kNearTaskWord = kLateQuadDrawWord + 4;
static const uint kLateTaskWord = kNearTaskWord + 3;
/// Splits a cell over task groups so a task payload stays small
static const uint kTaskGroupsPerCell = 2u;
/// Retest cells then every listed cell read back to tell whether occlusion culling pays
static const uint kOcclusionStatsWord = kLateTaskWord + 3;
static const uint kCellArgsWords = kOcclusionStatsWord + 2;

/// Bits are direction 3 slice 3 u 3 v 3 width-1 3 height-1 3 and brick in cell 6
uint packQuad(uint direction, uint slice, uint u, uint v, uint width, uint height, uint brickInCell)
{
    return direction | (slice << 3) | (u << 6) | (v << 9) | ((width - 1u) << 12) | ((height - 1u) << 15) | (brickInCell << 18);
}

/// Face plane axes per normal axis as rows over v with bits over u
uint quadAxisU(uint axis)
{
    return axis == 0u ? 1u : 0u;
}

uint quadAxisV(uint axis)
{
    return axis == 2u ? 1u : 2u;
}

/// Ten bits per axis
uint packCell(uint3 cell)
{
    return cell.x | (cell.y << 10) | (cell.z << 20);
}

uint3 unpackCell(uint packed)
{
    return uint3(packed & 0x3FFu, (packed >> 10) & 0x3FFu, packed >> 20);
}

/// First cell entry word with the instance in the low 20 bits and above it the occupied brick range as low xyz then high xyz
/// at 2 bits each
static const uint kCellInstanceBits = 20u;

uint packCellEntry(uint instanceIndex, uint3 brickLow, uint3 brickHigh)
{
    const uint bricks = brickLow.x | (brickLow.y << 2) | (brickLow.z << 4) | (brickHigh.x << 6) | (brickHigh.y << 8) |
                        (brickHigh.z << 10);
    return instanceIndex | (bricks << kCellInstanceBits);
}

uint cellEntryInstance(uint word)
{
    return word & ((1u << kCellInstanceBits) - 1u);
}

/// Voxel bounds of the occupied bricks of a cell clipped to the volume
void cellEntryBox(uint word, uint3 cell, int3 dims, out int3 boxMin, out int3 boxMax)
{
    const uint bricks = word >> kCellInstanceBits;
    const int3 low = int3(int(bricks & 3u), int((bricks >> 2) & 3u), int((bricks >> 4) & 3u));
    const int3 high = int3(int((bricks >> 6) & 3u), int((bricks >> 8) & 3u), int((bricks >> 10) & 3u));
    const int3 origin = int3(cell) * kCoarseVoxels;
    boxMin = origin + low * int(kBrickDim);
    boxMax = min(origin + (high + 1) * int(kBrickDim), dims);
}

static const uint kTagEmpty = 0;
static const uint kTagUniform = 1;
static const uint kTagShared = 2;
static const uint kTagOwned = 3;

struct RayHit
{
    bool hit;

    float t;

    int3 voxel;

    int axis;

    uint material;
    uint steps;

    /// Steps that skipped a whole coarse cell brick or 4x4x4 or 2x2x2 sub-brick so the rest are single voxels
    uint coarseSteps;
    uint brickSteps;
    uint subSteps;
};

uint gridEntry(uint volume, uint3 brick)
{
    const uint dimsX = gVolumeRecords[volume].brickDimsX;
    const uint dimsY = gVolumeRecords[volume].brickDimsY;
    return gVolumeGrids[gVolumeRecords[volume].gridOffset + brick.x + brick.y * dimsX + brick.z * (dimsX * dimsY)];
}

bool coarseSolid(uint volume, int3 cell)
{
    const uint dimsX = gVolumeRecords[volume].coarseDimsX;
    const uint dimsY = gVolumeRecords[volume].coarseDimsY;
    const uint dimsZ = gVolumeRecords[volume].coarseDimsZ;
    if (any(uint3(cell) >= uint3(dimsX, dimsY, dimsZ))) return false;
    
    const uint index = uint(cell.x) + uint(cell.y) * dimsX + uint(cell.z) * (dimsX * dimsY);
    return ((gVolumeCoarse[gVolumeRecords[volume].coarseOffset + (index >> 5)] >> (index & 31u)) & 1u) != 0;
}

uint pooledMaterial(uint slot, uint3 local)
{
    const uint index = (local.z << 6) | (local.y << 3) | local.x;
    const uint word = gBrickMaterials[(slot << 7) + (index >> 2)];
    return (word >> ((index & 3u) << 3)) & 0xFFu;
}

/// Get the palette ID for a voxel
uint voxelMaterialAt(uint volume, int3 voxel)
{
    const uint dimsX = gVolumeRecords[volume].brickDimsX;
    const uint dimsY = gVolumeRecords[volume].brickDimsY;
    const uint dimsZ = gVolumeRecords[volume].brickDimsZ;

    if (any(voxel < 0) || voxel.x >= int(dimsX * kBrickDim) || voxel.y >= int(dimsY * kBrickDim) || voxel.z >= int(dimsZ * kBrickDim))
    {
        return 0u;
    }
    
    // Shifts/masks instead of modulo arithmetic
    const uint entry = gridEntry(volume, uint3(voxel) >> 3);
    const uint tag = entry & 3u;
    if (tag == kTagEmpty)
    {
        return 0u;
    }
    if (tag == kTagUniform)
    {
        return entry >> 2;
    }
    return pooledMaterial(entry >> 2, uint3(voxel) & 7u);
}

float marchTieSlack(float t)
{
    return kMarchTieEpsilon * max(1.0, abs(t));
}

void leaveCell(float3 origin, float3 inverse, float3 stepSign, int3 cellMin, int size, inout float t, inout int axis, inout uint crossed)
{
    // Explicit scalarization ensures Slang does not reduce vector ternaries into single booleans
    float3 plane;
    plane.x = float(cellMin.x) + (stepSign.x > 0.0 ? float(size) : 0.0);
    plane.y = float(cellMin.y) + (stepSign.y > 0.0 ? float(size) : 0.0);
    plane.z = float(cellMin.z) + (stepSign.z > 0.0 ? float(size) : 0.0);
    
    float3 planeTimes;
    planeTimes.x = stepSign.x != 0.0 ? (plane.x - origin.x) * inverse.x : kFloatMax;
    planeTimes.y = stepSign.y != 0.0 ? (plane.y - origin.y) * inverse.y : kFloatMax;
    planeTimes.z = stepSign.z != 0.0 ? (plane.z - origin.z) * inverse.z : kFloatMax;
    
    float next = min(min(planeTimes.x, planeTimes.y), planeTimes.z);
    int nextAxis = planeTimes.x == next ? 0 : (planeTimes.y == next ? 1 : 2);
    
    float slackNext = next + marchTieSlack(next);
    crossed = (uint(planeTimes.x <= slackNext)) 
            | (uint(planeTimes.y <= slackNext) << 1) 
            | (uint(planeTimes.z <= slackNext) << 2);
    
    t = max(next, t);
    axis = nextAxis;
}

RayHit marchRay(uint volume, float3 origin, float3 direction, float tMin, float tMax)
{
    const int3 dims = int3(gVolumeRecords[volume].brickDimsX, gVolumeRecords[volume].brickDimsY, gVolumeRecords[volume].brickDimsZ) * int(kBrickDim);
    return marchRayBox(volume, origin, direction, tMin, tMax, int3(0, 0, 0), dims);
}

/// The box must lie inside the volume
/// Cells are chosen by integer index not float position since nudging positions skips voxels under grazing rays
RayHit marchRayBox(uint volume, float3 origin, float3 direction, float tMin, float tMax, int3 boxMin, int3 boxMax)
{
    RayHit result;
    result.hit = false;
    result.t = 0.0;
    result.voxel = int3(0, 0, 0);
    result.axis = -1;
    result.material = 0;
    result.steps = 0;
    result.coarseSteps = 0;
    result.brickSteps = 0;
    result.subSteps = 0;

    float tExit = kFloatMax;
    float3 entryTimes = float3(-kFloatMax, -kFloatMax, -kFloatMax);
    float3 inverse = float3(0.0, 0.0, 0.0);
    float3 stepSign = float3(0.0, 0.0, 0.0);

    for (int a = 0; a < 3; ++a)
    {
        if (direction[a] == 0.0)
        {
            if (origin[a] < float(boxMin[a]) || origin[a] > float(boxMax[a]))
            {
                return result;
            }
            continue;
        }

        inverse[a] = 1.0 / direction[a];
        stepSign[a] = direction[a] > 0.0 ? 1.0 : -1.0;

        const float t0 = (float(boxMin[a]) - origin[a]) * inverse[a];
        const float t1 = (float(boxMax[a]) - origin[a]) * inverse[a];
        entryTimes[a] = min(t0, t1);
        tExit = min(tExit, max(t0, t1));
    }

    float t = -kFloatMax;
    int axis = -1;
    for (int a = 0; a < 3; ++a)
    {
        if (entryTimes[a] > t)
        {
            t = entryTimes[a];
            axis = a;
        }
    }

    uint crossed = 0;
    if (t < tMin)
    {
        t = tMin;
        axis = -1;
    }
    else
    {
        for (int a = 0; a < 3; ++a)
        {
            if (entryTimes[a] >= t - marchTieSlack(t))
            {
                crossed |= 1u << uint(a);
            }
        }
    }

    tExit = min(tExit, tMax);
    if (t > tExit)
    {
        return result;
    }

    for (uint step = 0; step < kMaxMarchSteps; ++step)
    {
        result.steps = step + 1;
        if (t > tExit) return result;

        const float3 position = origin + direction * t;
        
        // Unrolled scalar assignment completely bypasses vector register allocation
        int3 voxel = int3(floor(position));
        if ((crossed & 1u) != 0) voxel.x = int(round(position.x)) - (stepSign.x < 0.0 ? 1 : 0);
        if ((crossed & 2u) != 0) voxel.y = int(round(position.y)) - (stepSign.y < 0.0 ? 1 : 0);
        if ((crossed & 4u) != 0) voxel.z = int(round(position.z)) - (stepSign.z < 0.0 ? 1 : 0);

        if (any(voxel < boxMin) || any(voxel >= boxMax)) return result;

        const int3 coarse = voxel >> 5; 
        if (!coarseSolid(volume, coarse))
        {
            ++result.coarseSteps;
            leaveCell(origin, inverse, stepSign, coarse << 5, kCoarseVoxels, t, axis, crossed);
            continue;
        }

        const int3 brick = voxel >> 3;
        const uint entry = gridEntry(volume, uint3(brick));
        const uint tag = entry & 3u;
    
        if (tag == kTagEmpty)
        {
            ++result.brickSteps;
            leaveCell(origin, inverse, stepSign, brick << 3, int(kBrickDim), t, axis, crossed);
            continue;
        }
    
        if (tag == kTagUniform)
        {
            result.hit = true;
            result.t = t;
            result.voxel = voxel;
            result.axis = axis;
            result.material = entry >> 2;
            return result;
        }
    
        // Pool words are 4x4x4 sub-bricks as gpu::packOccupancyWords lays them out
        const uint slot = entry >> 2;
        const uint3 local = uint3(voxel & 7u);
        const uint subBase = (slot << 4) + (((local.x >> 2) | ((local.y >> 2) << 1) | ((local.z >> 2) << 2)) << 1);
        const uint subLow = gBrickOccupancy[subBase];
        const uint subHigh = gBrickOccupancy[subBase + 1u];
        if ((subLow | subHigh) == 0u)
        {
            ++result.subSteps;
            leaveCell(origin, inverse, stepSign, voxel & int3(~3, ~3, ~3), 4, t, axis, crossed);
            continue;
        }

        // A 2x2x2 block never straddles the low and high word
        const uint subWord = (local.z & 2u) != 0u ? subHigh : subLow;
        if (((subWord >> ((local.x & 2u) | ((local.y & 2u) << 2))) & 0x00330033u) == 0u)
        {
            ++result.subSteps;
            leaveCell(origin, inverse, stepSign, voxel & int3(~1, ~1, ~1), 2, t, axis, crossed);
            continue;
        }

        if (((subWord >> ((local.x & 3u) | ((local.y & 3u) << 2) | ((local.z & 1u) << 4))) & 1u) != 0u)
        {
            result.hit = true;
            result.t = t;
            result.voxel = voxel;
            result.axis = axis;
            result.material = pooledMaterial(slot, local);
            return result;
        }
    
        leaveCell(origin, inverse, stepSign, voxel, 1, t, axis, crossed);
    }

    return result;
}

struct PaletteSample
{
    float3 albedo;
    float roughness;
    float metallic;

    float reflectivity;

    float emissive;

    float alpha;

    /// Physical material id where 0 also covers entries authored without one
    uint material;
};

float3 srgbToLinear(float3 value)
{
    return lerp(pow((value + 0.055) / 1.055, 2.4), value / 12.92, step(value, float3(0.04045, 0.04045, 0.04045)));
}

/// Word 0 bytes albedo rgb roughness and word 1 bytes metallic reflectivity emissive material
PaletteSample samplePalette(uint volume, uint index)
{
    const uint paletteOffset = gVolumeRecords[volume].paletteOffset;
    const float maxEmissive = gVolumeRecords[volume].maxEmissive;
    
    const uint base = (paletteOffset + index) * 4;
    const uint word0 = gPalettes[base];
    const uint word1 = gPalettes[base + 1];
    const uint word2 = gPalettes[base + 2];

    PaletteSample sample;
    sample.albedo = srgbToLinear(float3(word0 & 0xFFu, (word0 >> 8) & 0xFFu, (word0 >> 16) & 0xFFu) / 255.0);
    sample.roughness = float((word0 >> 24) & 0xFFu) / 255.0;
    sample.metallic = float(word1 & 0xFFu) / 255.0;
    sample.reflectivity = float((word1 >> 8) & 0xFFu) / 255.0;
    sample.emissive = float((word1 >> 16) & 0xFFu) / 255.0 * maxEmissive;
    sample.material = word1 >> 24;
    sample.alpha = float((word2 >> 16) & 0xFFu) / 255.0;
    return sample;
}

/// Meshing helpers shared by the compute mesher in voxel_cells.slang and the mesh shader in voxel_mesh.slang

/// Grid entry with the empty tag outside the volume
uint brickEntryAt(uint volume, int3 brick)
{
    const int3 dims = int3(gVolumeRecords[volume].brickDimsX, gVolumeRecords[volume].brickDimsY, gVolumeRecords[volume].brickDimsZ);
    if (any(brick < 0) || any(brick >= dims))
    {
        return kTagEmpty;
    }
    return gridEntry(volume, uint3(brick));
}

/// Occupancy word in the pooled layout so uniform bricks read as solid
uint occupancyWord(uint entry, uint word)
{
    const uint tag = entry & 3u;
    if (tag == kTagEmpty)
    {
        return 0u;
    }
    if (tag == kTagUniform)
    {
        return 0xFFFFFFFFu;
    }
    return gBrickOccupancy[((entry >> 2) << 4) + word];
}

/// Pool words are 4x4x4 sub-bricks so the meshers regather them as z-slices with bit x of byte y & 3 in word z * 2 + (y >> 2)
void loadBrickSlices(uint entry, out uint slices[16])
{
    uint sub[16];
    [ForceUnroll]
    for (uint word = 0u; word < 16u; ++word)
    {
        sub[word] = occupancyWord(entry, word);
    }

    [ForceUnroll]
    for (uint z = 0u; z < 8u; ++z)
    {
        [ForceUnroll]
        for (uint half = 0u; half < 2u; ++half)
        {
            // x 0-3 sub-brick then x 4-7 sub-brick two words on
            const uint low = (((half << 1) | ((z >> 2) << 2)) << 1) + ((z & 2u) >> 1);
            const uint high = low + 2u;
            uint slice = 0u;
            [ForceUnroll]
            for (uint y = 0u; y < 4u; ++y)
            {
                const uint shift = 4u * y + 16u * (z & 1u);
                slice |= (((sub[low] >> shift) & 0xFu) | (((sub[high] >> shift) & 0xFu) << 4)) << (8u * y);
            }
            slices[z * 2u + half] = slice;
        }
    }
}

/// Eight x bits of row y in slice z
uint occupancyRow(uint occupancy[16], uint z, uint y)
{
    return (occupancy[(z << 1) + (y >> 2)] >> ((y & 3u) << 3)) & 0xFFu;
}

/// Exposed faces of one slice as rows over v with bits over u
void exposedRows(uint occupancy[16], uint neighbour[16], uint axis, bool positive, uint slice, out uint rows[8])
{
    for (uint v = 0u; v < 8u; ++v)
    {
        uint mask = 0u;
        if (axis == 2u)
        {
            const uint current = occupancyRow(occupancy, slice, v);
            uint other;
            if (positive)
            {
                other = slice < 7u ? occupancyRow(occupancy, slice + 1u, v) : occupancyRow(neighbour, 0u, v);
            }
            else
            {
                other = slice > 0u ? occupancyRow(occupancy, slice - 1u, v) : occupancyRow(neighbour, 7u, v);
            }
            mask = current & ~other;
        }
        else if (axis == 1u)
        {
            const uint current = occupancyRow(occupancy, v, slice);
            uint other;
            if (positive)
            {
                other = slice < 7u ? occupancyRow(occupancy, v, slice + 1u) : occupancyRow(neighbour, v, 0u);
            }
            else
            {
                other = slice > 0u ? occupancyRow(occupancy, v, slice - 1u) : occupancyRow(neighbour, v, 7u);
            }
            mask = current & ~other;
        }
        else
        {
            for (uint y = 0u; y < 8u; ++y)
            {
                const uint row = occupancyRow(occupancy, v, y);
                uint other;
                if (positive)
                {
                    other = slice < 7u ? row >> (slice + 1u) : occupancyRow(neighbour, v, y);
                }
                else
                {
                    other = slice > 0u ? row >> (slice - 1u) : occupancyRow(neighbour, v, y) >> 7u;
                }
                mask |= (((row >> slice) & ~other) & 1u) << y;
            }
        }
        rows[v] = mask & 0xFFu;
    }
}

bool outsideFrustum(float4x4 voxelToClip, float3 boxMin, float3 boxMax)
{
    uint outside = 0x1Fu;
    for (uint i = 0u; i < 8u; ++i)
    {
        const float3 corner = lerp(boxMin, boxMax, kBoxCorners[i]);
        const float4 clip = mul(voxelToClip, float4(corner, 1.0));
        uint flags = 0u;
        flags |= clip.x < -clip.w ? 1u : 0u;
        flags |= clip.x > clip.w ? 2u : 0u;
        flags |= clip.y < -clip.w ? 4u : 0u;
        flags |= clip.y > clip.w ? 8u : 0u;
        flags |= clip.w <= 0.0 ? 16u : 0u;
        outside &= flags;
    }
    return outside != 0u;
}
