// Shared by voxel_mesh_task.slang and voxel_mesh.slang which compile apart since Slang gives task and mesh entry points of
// one module separate payload variables that NVIDIA drivers mishandle (shader-slang/slang#10123)
import voxel;
import voxel_dda;

static const uint kTaskBricks = 64u / kTaskGroupsPerCell;
/// Bit direction * 8 + slice of a meshlet face mask
static const uint kBrickSlices = 6u * 8u;
/// One meshlet per exposed slice of every brick side when no two fit together
static const uint kMaxMeshlets = kTaskBricks * kBrickSlices;
static const uint kMeshletQuads = 64u;

/// gPush.bucket picks the list since the near and late near lists share these entry points
static const uint kMeshEarly = 0u;

struct QuadTaskPayload
{
    uint entryIndex;
    /// x brickInCell in 6 bits then face mask bits 32 to 47 and y face mask bits 0 to 31
    uint2 meshlets[kMaxMeshlets];
};

uint3 brickOfCell(uint2 cell, uint brickInCell)
{
    return unpackCell(cell.y) * kCoarseBricks + uint3(brickInCell & 3u, (brickInCell >> 2) & 3u, brickInCell >> 4);
}

int3 directionStep(uint direction)
{
    int3 step = int3(0, 0, 0);
    step[direction >> 1] = (direction & 1u) == 0u ? 1 : -1;
    return step;
}

/// Exposed faces of one brick side in the occupancy layout with bit x of byte y & 3 of word z * 2 + (y >> 2)
/// Matches exposedRows slice by slice at a few operations per word
void exposedFaces(uint occupancy[16], uint neighbour[16], uint direction, out uint faces[16])
{
    for (uint word = 0u; word < 16u; ++word)
    {
        const uint occupied = occupancy[word];
        uint other;
        switch (direction)
        {
        case 0u:
            other = ((occupied >> 1) & 0x7F7F7F7Fu) | ((neighbour[word] & 0x01010101u) << 7);
            break;
        case 1u:
            other = ((occupied << 1) & 0xFEFEFEFEu) | ((neighbour[word] >> 7) & 0x01010101u);
            break;
        case 2u:
            other = (occupied >> 8) | (((word & 1u) == 0u ? occupancy[word + 1u] : neighbour[word - 1u]) << 24);
            break;
        case 3u:
            other = (occupied << 8) | (((word & 1u) == 1u ? occupancy[word - 1u] : neighbour[word + 1u]) >> 24);
            break;
        case 4u:
            other = word < 14u ? occupancy[word + 2u] : neighbour[word - 14u];
            break;
        default:
            other = word >= 2u ? occupancy[word - 2u] : neighbour[word + 14u];
            break;
        }
        faces[word] = occupied & ~other;
    }
}

uint sliceFaceCount(uint faces[16], uint axis, uint slice)
{
    uint count = 0u;
    for (uint word = 0u; word < 16u; ++word)
    {
        uint mask;
        if (axis == 0u)
        {
            mask = 0x01010101u << slice;
        }
        else if (axis == 1u)
        {
            mask = (word & 1u) == (slice >> 2) ? 0xFFu << ((slice & 3u) * 8u) : 0u;
        }
        else
        {
            mask = (word >> 1) == slice ? 0xFFFFFFFFu : 0u;
        }
        count += countbits(faces[word] & mask);
    }
    return count;
}

/// One slice of exposedFaces as the rows over v with bits over u that exposedRows gives
void sliceRows(uint faces[16], uint axis, uint slice, out uint rows[8])
{
    for (uint v = 0u; v < 8u; ++v)
    {
        if (axis == 2u)
        {
            rows[v] = (faces[slice * 2u + (v >> 2)] >> ((v & 3u) * 8u)) & 0xFFu;
        }
        else if (axis == 1u)
        {
            rows[v] = (faces[v * 2u + (slice >> 2)] >> ((slice & 3u) * 8u)) & 0xFFu;
        }
        else
        {
            // x is a bit inside each byte so the y bytes gather into one row
            const uint low = (faces[v * 2u] >> slice) & 0x01010101u;
            const uint high = (faces[v * 2u + 1u] >> slice) & 0x01010101u;
            rows[v] = (low & 1u) | ((low >> 7) & 2u) | ((low >> 14) & 4u) | ((low >> 21) & 8u) | ((high & 1u) << 4) |
                      ((high >> 3) & 32u) | ((high >> 10) & 64u) | ((high >> 17) & 128u);
        }
    }
}

/// Mirrors the slice tests of meshBrick in voxel_cells.slang
bool sliceFacesCamera(float3 camera, int3 brickMin, uint axis, bool positive, uint slice)
{
    const float plane = float(brickMin[axis] + int(slice)) + (positive ? 1.0 : 0.0);
    return positive ? camera[axis] > plane : camera[axis] < plane;
}
