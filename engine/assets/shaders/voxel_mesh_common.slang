// Shared by the camera and shadow task and mesh stages which compile apart since Slang gives task and mesh entry points of one
// module separate payload variables that NVIDIA drivers mishandle (shader-slang/slang#10123)
import voxel_dda;

static const uint kTaskBricks = 64u / kTaskGroupsPerCell;
/// Eye facing sides of a brick are at most 3 so its slices are at most 24
static const uint kBrickEntries = 3u * 8u;
static const uint kMaxEntries = kTaskBricks * kBrickEntries;
static const uint kMeshletQuads = 64u;

/// Mesh group m takes quads m * kMeshletQuads onward of the task group in entry order
struct QuadTaskPayload
{
    /// The camera cell list entry or the packCell coordinates of a shadow cell
    uint cell;
    uint entryCount;
    uint quadCount;
    /// brickInCell 6 bits then direction * 8 + slice 6 bits then the first quad of the slice 16 bits
    uint entries[kMaxEntries];
};

uint entryBrick(uint entry)
{
    return entry & 63u;
}

uint entrySlice(uint entry)
{
    return (entry >> 6) & 63u;
}

uint entryFirstQuad(uint entry)
{
    return entry >> 12;
}

uint3 brickOfCell(uint3 cell, uint brickInCell)
{
    return cell * kCoarseBricks + uint3(brickInCell & 3u, (brickInCell >> 2) & 3u, brickInCell >> 4);
}

int3 directionStep(uint direction)
{
    int3 step = int3(0, 0, 0);
    step[direction >> 1] = (direction & 1u) == 0u ? 1 : -1;
    return step;
}

/// Exposed faces of one brick side in the occupancy layout with bit x of byte y & 3 of word z * 2 + (y >> 2)
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

/// Mirrors the slice tests of meshBrick in voxel_cells.slang with w 1 for a point eye and 0 for a direction toward it
bool sliceFacesEye(float4 eye, int3 brickMin, uint axis, bool positive, uint slice)
{
    const float plane = float(brickMin[axis] + int(slice)) + (positive ? 1.0 : 0.0);
    const float side = eye[axis] - plane * eye.w;
    return positive ? side > 0.0 : side < 0.0;
}

interface IQuadSink
{
    [mutating]
    void quad(uint packedQuad);
}

/// Task and mesh stages must see the same quads in the same order
uint greedySlice<Sink : IQuadSink>(uint rows[8], uint direction, uint slice, uint brickInCell, inout Sink sink)
{
    uint count = 0u;
    for (uint v = 0u; v < 8u; ++v)
    {
        while (rows[v] != 0u)
        {
            const uint u = firstbitlow(rows[v]);
            const uint width = firstbitlow(~(rows[v] >> u));
            const uint run = ((1u << width) - 1u) << u;
            uint height = 1u;
            while (v + height < 8u && (rows[v + height] & run) == run)
            {
                rows[v + height] &= ~run;
                ++height;
            }
            rows[v] &= ~run;
            sink.quad(packQuad(direction, slice, u, v, width, height, brickInCell));
            ++count;
        }
    }
    return count;
}

struct CountSink : IQuadSink
{
    [mutating]
    void quad(uint packedQuad) { }
}

interface ISliceSink
{
    /// directionSlice is direction * 8 + slice
    [mutating]
    void slice(uint directionSlice, uint quads);
}

/// Greedy counts the quads of every eye facing slice of a brick that is not empty
void countBrickSlices<Sink : ISliceSink>(uint recordIndex, int3 brick, uint brickEntry, uint brickInCell, float4 eye, inout Sink sink)
{
    const int3 brickMin = brick * int(kBrickDim);
    uint occupancy[16];
    loadSolidSlices(recordIndex, brick, brickEntry, occupancy);

    CountSink counter;
    for (uint direction = 0u; direction < 6u; ++direction)
    {
        const uint axis = direction >> 1;
        const bool positive = (direction & 1u) == 0u;
        if (!sliceFacesEye(eye, brickMin, axis, positive, positive ? 0u : 7u))
        {
            continue;
        }

        const int3 neighbourBrick = brick + directionStep(direction);
        uint neighbour[16];
        loadSolidSlices(recordIndex, neighbourBrick, brickEntryAt(recordIndex, neighbourBrick), neighbour);

        uint faces[16];
        exposedFaces(occupancy, neighbour, direction, faces);
        for (uint slice = 0u; slice < 8u; ++slice)
        {
            if (!sliceFacesEye(eye, brickMin, axis, positive, slice))
            {
                continue;
            }
            uint rows[8];
            sliceRows(faces, axis, slice, rows);
            const uint count = greedySlice(rows, direction, slice, brickInCell, counter);
            if (count != 0u)
            {
                sink.slice(direction * 8u + slice, count);
            }
        }
    }
}

/// Quads of one payload entry again in the countBrickSlices order
void meshEntry<Sink : IQuadSink>(uint entry, uint recordIndex, uint3 cell, inout Sink sink)
{
    const uint brickInCell = entryBrick(entry);
    const uint direction = entrySlice(entry) >> 3;
    const uint slice = entrySlice(entry) & 7u;
    const int3 brick = int3(brickOfCell(cell, brickInCell));

    uint occupancy[16];
    uint neighbour[16];
    const int3 neighbourBrick = brick + directionStep(direction);
    loadSolidSlices(recordIndex, brick, brickEntryAt(recordIndex, brick), occupancy);
    loadSolidSlices(recordIndex, neighbourBrick, brickEntryAt(recordIndex, neighbourBrick), neighbour);

    uint faces[16];
    exposedFaces(occupancy, neighbour, direction, faces);
    uint rows[8];
    sliceRows(faces, direction >> 1, slice, rows);
    greedySlice(rows, direction, slice, brickInCell, sink);
}
