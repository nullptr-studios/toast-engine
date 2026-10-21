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
    /// Coarse buffer word of one bit per brick holding glass or kNoGlass
    uint glassOffset;
};

static const uint kNoGlass = 0xFFFFFFFFu;
/// voxel::k_entry_transparent in palette word 2
static const uint kEntryTransparent = 1u;

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

/// Voxel space corner 0 to 3 of a packQuad quad going round its plane
float3 quadCornerLocal(uint3 cell, uint packedQuad, uint corner)
{
    const uint direction = packedQuad & 7u;
    const uint slice = (packedQuad >> 3) & 7u;
    const uint u0 = (packedQuad >> 6) & 7u;
    const uint v0 = (packedQuad >> 9) & 7u;
    const uint width = ((packedQuad >> 12) & 7u) + 1u;
    const uint height = ((packedQuad >> 15) & 7u) + 1u;
    const uint brickInCell = (packedQuad >> 18) & 63u;

    const uint axis = direction >> 1;
    const uint3 brick = cell * kCoarseBricks + uint3(brickInCell & 3u, (brickInCell >> 2) & 3u, brickInCell >> 4);

    float3 local = float3(brick * kBrickDim);
    local[axis] += float(slice) + ((direction & 1u) == 0u ? 1.0 : 0.0);
    local[quadAxisU(axis)] += float(u0) + ((corner == 1u || corner == 2u) ? float(width) : 0.0);
    local[quadAxisV(axis)] += float(v0) + (corner >= 2u ? float(height) : 0.0);
    return local;
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

    /// Pyramid level the hit was accepted at where 0 is the voxel
    uint level;
};

uint gridEntry(VolumeRecord record, uint3 brick)
{
    return gVolumeGrids[record.gridOffset + brick.x + brick.y * record.brickDimsX + brick.z * (record.brickDimsX * record.brickDimsY)];
}

uint gridEntry(uint volume, uint3 brick)
{
    return gridEntry(gVolumeRecords[volume], brick);
}

bool coarseSolid(VolumeRecord record, int3 cell)
{
    if (any(uint3(cell) >= uint3(record.coarseDimsX, record.coarseDimsY, record.coarseDimsZ))) return false;

    const uint index = uint(cell.x) + uint(cell.y) * record.coarseDimsX + uint(cell.z) * (record.coarseDimsX * record.coarseDimsY);
    return ((gVolumeCoarse[record.coarseOffset + (index >> 5)] >> (index & 31u)) & 1u) != 0;
}

bool coarseSolid(uint volume, int3 cell)
{
    return coarseSolid(gVolumeRecords[volume], cell);
}

uint pooledMaterial(uint slot, uint3 local)
{
    const uint index = (local.z << 6) | (local.y << 3) | local.x;
    const uint word = gBrickMaterials[(slot << 7) + (index >> 2)];
    return (word >> ((index & 3u) << 3)) & 0xFFu;
}

/// Palette index of a voxel inside the brick behind @p entry
uint brickMaterialAt(uint entry, int3 voxel)
{
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

uint voxelMaterialAt(VolumeRecord record, int3 voxel)
{
    if (any(voxel < 0) || voxel.x >= int(record.brickDimsX * kBrickDim) || voxel.y >= int(record.brickDimsY * kBrickDim) ||
        voxel.z >= int(record.brickDimsZ * kBrickDim))
    {
        return 0u;
    }
    return brickMaterialAt(gridEntry(record, uint3(voxel) >> 3), voxel);
}

uint voxelMaterialAt(uint volume, int3 voxel)
{
    return voxelMaterialAt(gVolumeRecords[volume], voxel);
}

bool volumeHasGlass(uint volume)
{
    return gVolumeRecords[volume].glassOffset != kNoGlass;
}

/// Mirrors voxel::isGlass
bool glassEntry(VolumeRecord record, uint index)
{
    const uint word2 = gPalettes[(record.paletteOffset + index) * 4u + 2u];
    return index != 0u && (word2 & kEntryTransparent) != 0u && ((word2 >> 16) & 0xFFu) < 255u;
}

bool glassEntry(uint volume, uint index)
{
    return glassEntry(gVolumeRecords[volume], index);
}

float3 srgbToLinear(float3 value)
{
    return lerp(pow((value + 0.055) / 1.055, 2.4), value / 12.92, step(value, float3(0.04045, 0.04045, 0.04045)));
}

/// Below this a filtered ray counts as blocked
static const float kTintedOpaque = 0.01;

/// Voxels of alpha 1 glass that let exactly its albedo through
static const float kGlassTintVoxels = 4.0;

/// Beer Lambert through @p voxels of glass
float3 glassPathFilter(float3 albedo, float alpha, float voxels)
{
    return pow(max(albedo, float3(0.01, 0.01, 0.01)), alpha * max(voxels, 0.0) / kGlassTintVoxels);
}

/// Diffuse share of an entered surface
float glassScatter(float alpha, float roughness)
{
    return alpha * roughness;
}

bool glassBrick(VolumeRecord record, int3 brick)
{
    const uint3 dims = uint3(record.brickDimsX, record.brickDimsY, record.brickDimsZ);
    if (record.glassOffset == kNoGlass || any(brick < 0) || any(uint3(brick) >= dims))
    {
        return false;
    }
    const uint index = uint(brick.x) + uint(brick.y) * dims.x + uint(brick.z) * (dims.x * dims.y);
    return ((gVolumeCoarse[record.glassOffset + (index >> 5)] >> (index & 31u)) & 1u) != 0u;
}

bool glassBrick(uint volume, int3 brick)
{
    return glassBrick(gVolumeRecords[volume], brick);
}

interface IGlassPass
{
    /// False once no light passes
    [mutating]
    bool pass(VolumeRecord record, uint material, float tEnter, float tExit, float voxelsPerT);

    /// Coarse floors would take glass as opaque
    bool filters();
}

struct SkipGlass : IGlassPass
{
    [mutating]
    bool pass(VolumeRecord record, uint material, float tEnter, float tExit, float voxelsPerT)
    {
        return true;
    }

    bool filters()
    {
        return false;
    }
}

/// Mirrors compositeGlass in voxel_glass.slang at head on Fresnel
struct TintGlass : IGlassPass
{
    float3 transmittance;
    /// End of the last entry so a contiguous one joins its run
    float runEnd;

    [mutating]
    bool pass(VolumeRecord record, uint material, float tEnter, float tExit, float voxelsPerT)
    {
        const uint base = (record.paletteOffset + material) * 4u;
        const uint word0 = gPalettes[base];
        const uint word2 = gPalettes[base + 2u];
        const float alpha = float((word2 >> 16) & 0xFFu) / 255.0;
        const float roughness = float(word0 >> 24) / 255.0;
        const float3 albedo = srgbToLinear(float3(word0 & 0xFFu, (word0 >> 8) & 0xFFu, (word0 >> 16) & 0xFFu) / 255.0);
        transmittance *= glassPathFilter(albedo, alpha, (tExit - tEnter) * voxelsPerT);

        // voxel::iorOf
        const uint iorByte = word2 >> 24;
        const float f0 = iorF0(iorByte == 0u ? kDefaultIor : 1.0 + float(iorByte) / 100.0);
        if (abs(tEnter - runEnd) * voxelsPerT > 1e-3)
        {
            transmittance *= (1.0 - f0) * (1.0 - glassScatter(alpha, roughness));
        }
        runEnd = tExit;
        return max(transmittance.x, max(transmittance.y, transmittance.z)) > kTintedOpaque;
    }

    bool filters()
    {
        return true;
    }
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

    // A plane at or behind t stalls the ray until kMaxMarchSteps
    t = next > t ? next : t + marchTieSlack(t);
    axis = nextAxis;
}

RayHit marchRay(uint volume, float3 origin, float3 direction, float tMin, float tMax, bool throughGlass = true)
{
    const int3 dims = int3(gVolumeRecords[volume].brickDimsX, gVolumeRecords[volume].brickDimsY, gVolumeRecords[volume].brickDimsZ) * int(kBrickDim);
    return marchRayBox(volume, origin, direction, tMin, tMax, int3(0, 0, 0), dims, throughGlass);
}

struct MarchState
{
    float t;
    float tExit;
    float3 inverse;
    float3 stepSign;
    /// Axis of the last plane crossed and -1 before any
    int axis;
    /// Axes crossed at t within the tie slack
    uint crossed;
};

/// False when the ray misses the box or meets it only past tMax
bool beginMarch(float3 origin, float3 direction, float tMin, float tMax, int3 boxMin, int3 boxMax, out MarchState march)
{
    march.t = 0.0;
    march.tExit = kFloatMax;
    march.inverse = float3(0.0, 0.0, 0.0);
    march.stepSign = float3(0.0, 0.0, 0.0);
    march.axis = -1;
    march.crossed = 0u;

    float3 entryTimes = float3(-kFloatMax, -kFloatMax, -kFloatMax);
    for (int a = 0; a < 3; ++a)
    {
        if (direction[a] == 0.0)
        {
            if (origin[a] < float(boxMin[a]) || origin[a] > float(boxMax[a]))
            {
                return false;
            }
            continue;
        }

        march.inverse[a] = 1.0 / direction[a];
        march.stepSign[a] = direction[a] > 0.0 ? 1.0 : -1.0;

        const float t0 = (float(boxMin[a]) - origin[a]) * march.inverse[a];
        const float t1 = (float(boxMax[a]) - origin[a]) * march.inverse[a];
        entryTimes[a] = min(t0, t1);
        march.tExit = min(march.tExit, max(t0, t1));
    }

    march.t = -kFloatMax;
    for (int a = 0; a < 3; ++a)
    {
        if (entryTimes[a] > march.t)
        {
            march.t = entryTimes[a];
            march.axis = a;
        }
    }

    if (march.t < tMin)
    {
        march.t = tMin;
        march.axis = -1;
    }
    else
    {
        for (int a = 0; a < 3; ++a)
        {
            if (entryTimes[a] >= march.t - marchTieSlack(march.t))
            {
                march.crossed |= 1u << uint(a);
            }
        }
    }

    march.tExit = min(march.tExit, tMax);
    return march.t <= march.tExit;
}

/// Crossed axes come from the plane not the rounded position
int3 marchVoxel(float3 origin, float3 direction, MarchState march)
{
    const float3 position = origin + direction * march.t;

    int3 voxel = int3(floor(position));
    if ((march.crossed & 1u) != 0) voxel.x = int(round(position.x)) - (march.stepSign.x < 0.0 ? 1 : 0);
    if ((march.crossed & 2u) != 0) voxel.y = int(round(position.y)) - (march.stepSign.y < 0.0 ? 1 : 0);
    if ((march.crossed & 4u) != 0) voxel.z = int(round(position.z)) - (march.stepSign.z < 0.0 ? 1 : 0);
    return voxel;
}

void marchLeave(float3 origin, inout MarchState march, int3 cellMin, int size)
{
    leaveCell(origin, march.inverse, march.stepSign, cellMin, size, march.t, march.axis, march.crossed);
}

/// Coarsest pyramid level a hit may be accepted at for a given t with 0 the voxel and 4 the coarse cell
interface IHitFloor
{
    uint level(float t);
}

struct ExactFloor : IHitFloor
{
    uint level(float t)
    {
        return 0u;
    }
}

/// Accepts a cell once it fits the cone footprint
struct ConeFloor : IHitFloor
{
    float tMin;
    /// Cone slope times voxels per unit t
    float slope;

    uint level(float t)
    {
        const float footprint = max(t - tMin, 0.0) * slope;
        return footprint >= float(kCoarseVoxels) ? 4u : (footprint >= 8.0 ? 3u : (footprint >= 4.0 ? 2u : (footprint >= 2.0 ? 1u : 0u)));
    }
}

/// The box must lie inside the volume
/// Integer cell index since nudged positions skip voxels under grazing rays
/// Passes through glass unlike toast/voxel/ray_march.cpp
RayHit marchCore<Floor : IHitFloor, Glass : IGlassPass>(uint volume, float3 origin, float3 direction, float tMin, float tMax,
                                                       int3 boxMin, int3 boxMax, bool throughGlass, Floor hitFloor,
                                                       inout Glass glass)
{
    const VolumeRecord record = gVolumeRecords[volume];
    const bool skipGlass = throughGlass && record.glassOffset != kNoGlass;
    const bool filterGlass = skipGlass && glass.filters();
    const float voxelsPerT = length(direction);

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
    result.level = 0;

    MarchState march;
    if (!beginMarch(origin, direction, tMin, tMax, boxMin, boxMax, march))
    {
        return result;
    }

    const int3 extent = boxMax - boxMin;
    const uint stepLimit = min(kMaxMarchSteps, uint(extent.x + extent.y + extent.z) + 8u);
    for (uint step = 0; step < stepLimit; ++step)
    {
        result.steps = step + 1;
        if (march.t > march.tExit) return result;

        const int3 voxel = marchVoxel(origin, direction, march);
        if (any(voxel < boxMin) || any(voxel >= boxMax)) return result;

        uint floorLevel = hitFloor.level(march.t);
        if (filterGlass)
        {
            floorLevel = min(floorLevel, 3u);
        }

        const int3 coarse = voxel >> 5;
        if (!coarseSolid(record, coarse))
        {
            ++result.coarseSteps;
            marchLeave(origin, march, coarse << 5, kCoarseVoxels);
            continue;
        }
        if (floorLevel >= 4u)
        {
            result.hit = true;
            result.t = march.t;
            result.voxel = voxel;
            result.axis = march.axis;
            result.level = 4u;
            return result;
        }

        const int3 brick = voxel >> 3;
        const uint entry = gridEntry(record, uint3(brick));
        const uint tag = entry & 3u;

        if (tag == kTagEmpty)
        {
            ++result.brickSteps;
            marchLeave(origin, march, brick << 3, int(kBrickDim));
            continue;
        }

        // A uniform brick is full so its hit is exact at any floor
        if (tag == kTagUniform)
        {
            if (skipGlass && glassEntry(record, entry >> 2))
            {
                ++result.brickSteps;
                const float entered = march.t;
                marchLeave(origin, march, brick << 3, int(kBrickDim));
                if (glass.pass(record, entry >> 2, entered, min(march.t, march.tExit), voxelsPerT))
                {
                    continue;
                }
                result.hit = true;
                result.t = entered;
                result.voxel = voxel;
                result.material = entry >> 2;
                return result;
            }
            result.hit = true;
            result.t = march.t;
            result.voxel = voxel;
            result.axis = march.axis;
            result.material = entry >> 2;
            return result;
        }
        if (filterGlass && floorLevel > 0u && glassBrick(record, brick))
        {
            floorLevel = 0u;
        }
        if (floorLevel >= 3u)
        {
            result.hit = true;
            result.t = march.t;
            result.voxel = voxel;
            result.axis = march.axis;
            result.level = 3u;
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
            marchLeave(origin, march, voxel & int3(~3, ~3, ~3), 4);
            continue;
        }
        if (floorLevel >= 2u)
        {
            result.hit = true;
            result.t = march.t;
            result.voxel = voxel;
            result.axis = march.axis;
            result.level = 2u;
            return result;
        }

        // A 2x2x2 block never straddles the low and high word
        const uint subWord = (local.z & 2u) != 0u ? subHigh : subLow;
        if (((subWord >> ((local.x & 2u) | ((local.y & 2u) << 2))) & 0x00330033u) == 0u)
        {
            ++result.subSteps;
            marchLeave(origin, march, voxel & int3(~1, ~1, ~1), 2);
            continue;
        }
        if (floorLevel >= 1u)
        {
            result.hit = true;
            result.t = march.t;
            result.voxel = voxel;
            result.axis = march.axis;
            result.level = 1u;
            return result;
        }

        if (((subWord >> ((local.x & 3u) | ((local.y & 3u) << 2) | ((local.z & 1u) << 4))) & 1u) != 0u)
        {
            const uint material = pooledMaterial(slot, local);
            const float entered = march.t;
            const int enteredAxis = march.axis;
            const bool opaque = !skipGlass || !glassEntry(record, material);
            if (!opaque)
            {
                marchLeave(origin, march, voxel, 1);
            }
            if (opaque || !glass.pass(record, material, entered, min(march.t, march.tExit), voxelsPerT))
            {
                result.hit = true;
                result.t = entered;
                result.voxel = voxel;
                result.axis = enteredAxis;
                result.material = material;
                return result;
            }
            continue;
        }

        marchLeave(origin, march, voxel, 1);
    }

    result.steps = kMaxMarchSteps;
    return result;
}

RayHit marchRayBox(uint volume, float3 origin, float3 direction, float tMin, float tMax, int3 boxMin, int3 boxMax, bool throughGlass = true)
{
    ExactFloor exact;
    SkipGlass skip;
    return marchCore(volume, origin, direction, tMin, tMax, boxMin, boxMax, throughGlass, exact, skip);
}

/// Sparse tier for light visibility and specular rays
/// Hits at the coarsest occupied cell within the cone so never later than the dense hit
/// With coneSlope 0 it is the dense tier
RayHit marchSparse(uint volume, float3 origin, float3 direction, float tMin, float tMax, float coneSlope, bool throughGlass = true)
{
    const int3 dims = int3(gVolumeRecords[volume].brickDimsX, gVolumeRecords[volume].brickDimsY, gVolumeRecords[volume].brickDimsZ) * int(kBrickDim);
    ConeFloor cone;
    cone.tMin = tMin;
    cone.slope = coneSlope * length(direction);
    SkipGlass skip;
    return marchCore(volume, origin, direction, tMin, tMax, int3(0, 0, 0), dims, throughGlass, cone, skip);
}

/// marchSparse filtering by glass colour
RayHit marchSparseTinted(uint volume, float3 origin, float3 direction, float tMin, float tMax, float coneSlope,
                         inout float3 transmittance)
{
    const int3 dims = int3(gVolumeRecords[volume].brickDimsX, gVolumeRecords[volume].brickDimsY, gVolumeRecords[volume].brickDimsZ) * int(kBrickDim);
    ConeFloor cone;
    cone.tMin = tMin;
    cone.slope = coneSlope * length(direction);
    TintGlass tint;
    tint.transmittance = transmittance;
    tint.runEnd = -kFloatMax;
    const RayHit hit = marchCore(volume, origin, direction, tMin, tMax, int3(0, 0, 0), dims, true, cone, tint);
    transmittance = tint.transmittance;
    return hit;
}

/// Levels of the occupancy pyramid the super-sparse tier samples
static const uint kLevelCoarse = 4u;

/// Glass counts only at the voxel and uniform bricks
bool occupiedAtLevel(VolumeRecord record, int3 voxel, uint level, bool skipGlass)
{
    if (!coarseSolid(record, voxel >> 5))
    {
        return false;
    }
    if (level == kLevelCoarse)
    {
        return true;
    }

    const uint entry = gridEntry(record, uint3(voxel >> 3));
    const uint tag = entry & 3u;
    if (tag == kTagEmpty)
    {
        return false;
    }
    if (tag == kTagUniform)
    {
        return !skipGlass || !glassEntry(record, entry >> 2);
    }
    if (level == 3u)
    {
        return true;
    }

    const uint slot = entry >> 2;
    const uint3 local = uint3(voxel & 7u);
    const uint subBase = (slot << 4) + (((local.x >> 2) | ((local.y >> 2) << 1) | ((local.z >> 2) << 2)) << 1);
    const uint subLow = gBrickOccupancy[subBase];
    const uint subHigh = gBrickOccupancy[subBase + 1u];
    if (level == 2u)
    {
        return (subLow | subHigh) != 0u;
    }

    const uint subWord = (local.z & 2u) != 0u ? subHigh : subLow;
    if (level == 1u)
    {
        return ((subWord >> ((local.x & 2u) | ((local.y & 2u) << 2))) & 0x00330033u) != 0u;
    }

    if (((subWord >> ((local.x & 3u) | ((local.y & 3u) << 2) | ((local.z & 1u) << 4))) & 1u) == 0u)
    {
        return false;
    }
    return !skipGlass || !glassEntry(record, pooledMaterial(slot, local));
}

/// Super-sparse tier for ambient and long occlusion rays
/// Five fixed samples firstStep voxels apart then three doubling ones each tested at the level its step spans
RayHit marchSuperSparse(uint volume, float3 origin, float3 direction, float tMin, float tMax, float firstStep, bool throughGlass = true)
{
    const VolumeRecord record = gVolumeRecords[volume];
    const bool skipGlass = throughGlass && record.glassOffset != kNoGlass;
    const int3 dims = int3(record.brickDimsX, record.brickDimsY, record.brickDimsZ) * int(kBrickDim);

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
    result.level = 0;

    const float3 safeDirection = select(abs(direction) > 1e-12, direction, float3(1e-12, 1e-12, 1e-12));
    const float3 inverse = 1.0 / safeDirection;
    const float3 near = (float3(0.0, 0.0, 0.0) - origin) * inverse;
    const float3 far = (float3(dims) - origin) * inverse;
    const float3 enter = min(near, far);
    const float3 leave = max(near, far);
    const float tEnter = max(max(enter.x, enter.y), max(enter.z, tMin));
    const float tExit = min(min(leave.x, leave.y), min(leave.z, tMax));
    if (tEnter > tExit)
    {
        return result;
    }

    const float tPerVoxel = 1.0 / max(length(direction), 1e-8);
    float previous = 0.0;
    [unroll]
    for (uint sample = 0u; sample < 8u; ++sample)
    {
        const float distance = sample < 5u ? firstStep * float(sample + 1u) : firstStep * 5.0 * exp2(float(sample) - 4.0);
        const float t = tMin + distance * tPerVoxel;
        if (t > tExit)
        {
            return result;
        }
        result.steps = sample + 1u;

        const float span = distance - previous;
        previous = distance;
        if (t < tEnter)
        {
            continue;
        }
        const int3 voxel = int3(floor(origin + direction * t));
        if (any(voxel < int3(0, 0, 0)) || any(voxel >= dims))
        {
            continue;
        }

        const uint level = span <= 1.0 ? 0u : (span <= 2.0 ? 1u : (span <= 4.0 ? 2u : (span <= 8.0 ? 3u : kLevelCoarse)));
        if (occupiedAtLevel(record, voxel, level, skipGlass))
        {
            result.hit = true;
            result.t = t;
            result.voxel = voxel;
            result.level = level;
            return result;
        }
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

    float ior;

    /// Physical material id where 0 also covers entries authored without one
    uint material;
};

/// Mirrors voxel::k_default_ior
static const float kDefaultIor = 1.5;

/// Head on reflectance of a dielectric against air
float iorF0(float ior)
{
    const float r = (ior - 1.0) / (ior + 1.0);
    return r * r;
}

/// Word 0 bytes albedo rgb roughness and word 1 bytes metallic reflectivity emissive material
PaletteSample samplePalette(VolumeRecord record, uint index)
{
    const uint paletteOffset = record.paletteOffset;
    const float maxEmissive = record.maxEmissive;

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
    // voxel::iorOf
    const uint iorByte = word2 >> 24;
    sample.ior = iorByte == 0u ? kDefaultIor : 1.0 + float(iorByte) / 100.0;
    return sample;
}

PaletteSample samplePalette(uint volume, uint index)
{
    return samplePalette(gVolumeRecords[volume], index);
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

/// loadBrickSlices minus glass voxels so opaque faces against glass are exposed and glass meshes nothing
void loadSolidSlices(uint volume, int3 brick, uint entry, out uint slices[16])
{
    loadBrickSlices(entry, slices);
    if (!glassBrick(volume, brick))
    {
        return;
    }

    const uint tag = entry & 3u;
    if (tag == kTagUniform)
    {
        if (glassEntry(volume, entry >> 2))
        {
            [ForceUnroll]
            for (uint word = 0u; word < 16u; ++word)
            {
                slices[word] = 0u;
            }
        }
        return;
    }

    // Material word w holds x 4 * (w & 1) onward of row (w >> 1) & 7 in slice w >> 4
    const uint base = (entry >> 2) << 7;
    for (uint word = 0u; word < kMaterialWordsPerBrick; ++word)
    {
        const uint bytes = gBrickMaterials[base + word];
        if (bytes == 0u)
        {
            continue;
        }
        uint glass = 0u;
        for (uint b = 0u; b < 4u; ++b)
        {
            if (glassEntry(volume, (bytes >> (b * 8u)) & 0xFFu))
            {
                glass |= 1u << b;
            }
        }
        const uint y = (word >> 1) & 7u;
        slices[(word >> 4) * 2u + (y >> 2)] &= ~(glass << (((word & 1u) << 2) + ((y & 3u) << 3)));
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
