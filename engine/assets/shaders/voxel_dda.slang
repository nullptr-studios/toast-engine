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

/// Mirrors renderer::VoxelPass::InstanceGpu
struct VoxelInstance
{
    /// Voxel (x y z) occupies [x x + 1] in this space
    float4x4 voxelToWorld;
    float4x4 worldToVoxel;
    uint recordIndex;
    uint pad0;
    uint pad1;
    uint pad2;
};

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

    /// Steps that skipped a whole coarse cell or brick so the rest are single voxels
    uint coarseSteps;
    uint brickSteps;
};

uint gridEntry(VolumeRecord record, uint3 brick)
{
    return gVolumeGrids[record.gridOffset + brick.x + brick.y * record.brickDimsX + brick.z * record.brickDimsX * record.brickDimsY];
}

bool coarseSolid(VolumeRecord record, int3 cell)
{
    if (any(cell < 0) || cell.x >= int(record.coarseDimsX) || cell.y >= int(record.coarseDimsY) || cell.z >= int(record.coarseDimsZ))
    {
        return false;
    }
    const uint index = uint(cell.x) + uint(cell.y) * record.coarseDimsX + uint(cell.z) * record.coarseDimsX * record.coarseDimsY;
    return ((gVolumeCoarse[record.coarseOffset + index / 32] >> (index % 32)) & 1u) != 0;
}

/// The 64 bits of slice z are two words low half first
bool pooledSolid(uint slot, uint3 local)
{
    const uint bit = local.y * kBrickDim + local.x;
    const uint word = gBrickOccupancy[slot * kOccupancyWordsPerBrick + local.z * 2 + bit / 32];
    return ((word >> (bit % 32)) & 1u) != 0;
}

/// 512 bytes packed four to a word little endian
uint pooledMaterial(uint slot, uint3 local)
{
    const uint index = local.x + local.y * kBrickDim + local.z * kBrickDim * kBrickDim;
    const uint word = gBrickMaterials[slot * kMaterialWordsPerBrick + index / 4];
    return (word >> (8 * (index % 4))) & 0xFFu;
}

float marchTieSlack(float t)
{
    return kMarchTieEpsilon * max(1.0, abs(t));
}

void leaveCell(float3 origin, float3 inverse, float3 stepSign, int3 cellMin, int size, inout float t, inout int axis, inout uint crossed)
{
    float3 planeTimes = float3(kFloatMax, kFloatMax, kFloatMax);
    float next = kFloatMax;
    int nextAxis = axis;
    for (int a = 0; a < 3; ++a)
    {
        if (stepSign[a] == 0.0)
        {
            continue;
        }
        const float plane = float(stepSign[a] > 0.0 ? cellMin[a] + size : cellMin[a]);
        planeTimes[a] = (plane - origin[a]) * inverse[a];
        if (planeTimes[a] < next)
        {
            next = planeTimes[a];
            nextAxis = a;
        }
    }

    crossed = 0;
    for (int a = 0; a < 3; ++a)
    {
        if (planeTimes[a] <= next + marchTieSlack(next))
        {
            crossed |= 1u << uint(a);
        }
    }
    t = max(next, t);
    axis = nextAxis;
}

/// Cells are chosen by integer index not float position since nudging positions skips voxels under grazing rays
RayHit marchRay(uint volume, float3 origin, float3 direction, float tMin, float tMax)
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

    const VolumeRecord record = gVolumeRecords[volume];
    const uint3 brickDims = uint3(record.brickDimsX, record.brickDimsY, record.brickDimsZ);
    const int3 dims = int3(brickDims) * int(kBrickDim);

    float tExit = kFloatMax;
    float3 entryTimes = float3(-kFloatMax, -kFloatMax, -kFloatMax);
    float3 inverse = float3(0.0, 0.0, 0.0);
    float3 stepSign = float3(0.0, 0.0, 0.0);

    for (int a = 0; a < 3; ++a)
    {
        if (direction[a] == 0.0)
        {
            if (origin[a] < 0.0 || origin[a] > float(dims[a]))
            {
                return result;
            }
            continue;
        }

        inverse[a] = 1.0 / direction[a];
        stepSign[a] = direction[a] > 0.0 ? 1.0 : -1.0;

        const float t0 = (0.0 - origin[a]) * inverse[a];
        const float t1 = (float(dims[a]) - origin[a]) * inverse[a];
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
        if (t > tExit)
        {
            return result;
        }

        const float3 position = origin + direction * t;
        int3 voxel = int3(floor(position));
        for (int a = 0; a < 3; ++a)
        {
            if ((crossed & (1u << uint(a))) != 0)
            {
                const int plane = int(round(position[a]));
                voxel[a] = stepSign[a] > 0.0 ? plane : plane - 1;
            }
        }
        if (any(voxel < 0) || any(voxel >= dims))
        {
            return result;
        }

        const int3 coarse = voxel / kCoarseVoxels;
        if (!coarseSolid(record, coarse))
        {
            ++result.coarseSteps;
            leaveCell(origin, inverse, stepSign, coarse * kCoarseVoxels, kCoarseVoxels, t, axis, crossed);
            continue;
        }

        const int3 brick = voxel / int(kBrickDim);
        const uint entry = gridEntry(record, uint3(brick));
        const uint tag = entry & 3u;

        if (tag == kTagEmpty)
        {
            ++result.brickSteps;
            leaveCell(origin, inverse, stepSign, brick * int(kBrickDim), int(kBrickDim), t, axis, crossed);
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

        const uint slot = entry >> 2;
        const uint3 local = uint3(voxel - brick * int(kBrickDim));
        if (pooledSolid(slot, local))
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
PaletteSample samplePalette(VolumeRecord record, uint index)
{
    const uint base = (record.paletteOffset + index) * 4;
    const uint word0 = gPalettes[base];
    const uint word1 = gPalettes[base + 1];
    const uint word2 = gPalettes[base + 2];

    PaletteSample sample;
    sample.albedo = srgbToLinear(float3(word0 & 0xFFu, (word0 >> 8) & 0xFFu, (word0 >> 16) & 0xFFu) / 255.0);
    sample.roughness = float((word0 >> 24) & 0xFFu) / 255.0;
    sample.metallic = float(word1 & 0xFFu) / 255.0;
    sample.reflectivity = float((word1 >> 8) & 0xFFu) / 255.0;
    sample.emissive = float((word1 >> 16) & 0xFFu) / 255.0 * record.maxEmissive;
    sample.material = word1 >> 24;
    sample.alpha = float((word2 >> 16) & 0xFFu) / 255.0;
    return sample;
}
