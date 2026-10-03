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
    /// Bit 0 set means previousVoxelToWorld is not a real previous frame
    uint flags;
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

bool pooledSolid(uint slot, uint3 local)
{
    // wordIdx decouples from local.x, bitIdx decouples from local.z
    const uint wordIdx = (slot << 4) + (local.z << 1) + (local.y >> 2);
    const uint bitIdx = ((local.y & 3u) << 3) | local.x;
    return ((gBrickOccupancy[wordIdx] >> bitIdx) & 1u) != 0;
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

    // Read ONLY the extents to avoid keeping a full struct alive
    const uint dimsX = gVolumeRecords[volume].brickDimsX;
    const uint dimsY = gVolumeRecords[volume].brickDimsY;
    const uint dimsZ = gVolumeRecords[volume].brickDimsZ;
    const int3 dims = int3(dimsX, dimsY, dimsZ) * int(kBrickDim);

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
        if (t > tExit) return result;

        const float3 position = origin + direction * t;
        
        // Unrolled scalar assignment completely bypasses vector register allocation
        int3 voxel = int3(floor(position));
        if ((crossed & 1u) != 0) voxel.x = int(round(position.x)) - (stepSign.x < 0.0 ? 1 : 0);
        if ((crossed & 2u) != 0) voxel.y = int(round(position.y)) - (stepSign.y < 0.0 ? 1 : 0);
        if ((crossed & 4u) != 0) voxel.z = int(round(position.z)) - (stepSign.z < 0.0 ? 1 : 0);

        if (any(uint3(voxel) >= uint3(dims))) return result;

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
    
        const uint slot = entry >> 2;
        const uint3 local = uint3(voxel & 7u); 
        
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