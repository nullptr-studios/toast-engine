// Mesh stage of the near cell mesh shader backend kept out of voxel.slang since its SPIR-V declares mesh shading
// One group per window of kMeshletQuads quads and one thread per slice of the window greedy merges its 8x8 faces again
import voxel;
import voxel_dda;
import voxel_mesh_common;
import lighting;

groupshared uint sFirstEntry;
groupshared uint sQuads[kMeshletQuads];

/// next is the global index of the next quad of the slice
struct WindowSink : IQuadSink
{
    uint next;
    uint windowStart;

    [mutating]
    void quad(uint packedQuad)
    {
        if (next - windowStart < kMeshletQuads)
        {
            sQuads[next - windowStart] = packedQuad;
        }
        ++next;
    }
}

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(kMeshletQuads, 1, 1)]
void quadMeshMain(uint3 group : SV_GroupID, uint thread : SV_GroupIndex, in payload QuadTaskPayload payload,
                  OutputVertices<QuadVSOutput, kMeshletQuads * 4> vertices, OutputIndices<uint3, kMeshletQuads * 2> triangles)
{
    const uint windowStart = group.x * kMeshletQuads;
    const uint quads = min(payload.quadCount - windowStart, kMeshletQuads);

    // Last slice starting at or before the window so a slice split across two windows meshes in both
    if (thread == 0u)
    {
        uint low = 0u;
        uint high = payload.entryCount;
        while (high - low > 1u)
        {
            const uint middle = (low + high) / 2u;
            if (entryFirstQuad(payload.entries[middle]) <= windowStart)
            {
                low = middle;
            }
            else
            {
                high = middle;
            }
        }
        sFirstEntry = low;
    }
    GroupMemoryBarrierWithGroupSync();

    const uint2 cell = gVoxelCells[payload.entryIndex];
    const VoxelInstance instance = gVoxelInstances[cellEntryInstance(cell.x)];

    // Every slice holds a quad so a window spans at most kMeshletQuads slices
    const uint index = sFirstEntry + thread;
    const uint entry = index < payload.entryCount ? payload.entries[index] : 0u;
    if (index < payload.entryCount && entryFirstQuad(entry) < windowStart + kMeshletQuads)
    {
        const uint brickInCell = entryBrick(entry);
        const uint direction = entrySlice(entry) >> 3;
        const uint slice = entrySlice(entry) & 7u;
        const int3 brick = int3(brickOfCell(cell, brickInCell));

        const uint brickEntry = brickEntryAt(instance.recordIndex, brick);
        const uint neighbourEntry = brickEntryAt(instance.recordIndex, brick + directionStep(direction));
        uint occupancy[16];
        uint neighbour[16];
        loadBrickSlices(brickEntry, occupancy);
        loadBrickSlices(neighbourEntry, neighbour);

        uint faces[16];
        exposedFaces(occupancy, neighbour, direction, faces);
        uint rows[8];
        sliceRows(faces, direction >> 1, slice, rows);
        WindowSink sink = { entryFirstQuad(entry), windowStart };
        greedySlice(rows, direction, slice, brickInCell, sink);
    }
    GroupMemoryBarrierWithGroupSync();

    SetMeshOutputCounts(quads * 4u, quads * 2u);
    if (thread < quads)
    {
        const uint packedQuad = sQuads[thread];
        const uint first = thread * 4u;
        for (uint corner = 0u; corner < 4u; ++corner)
        {
            vertices[first + corner] = quadCorner(cell, packedQuad, corner);
        }
        if (quadFlipped(instance, packedQuad))
        {
            triangles[thread * 2u] = uint3(first, first + 2u, first + 1u);
            triangles[thread * 2u + 1u] = uint3(first, first + 3u, first + 2u);
        }
        else
        {
            triangles[thread * 2u] = uint3(first, first + 1u, first + 2u);
            triangles[thread * 2u + 1u] = uint3(first, first + 2u, first + 3u);
        }
    }
}

[shader("fragment")]
[earlydepthstencil]
FSOutput meshQuadFragmentMain(QuadVSOutput input)
{
    return shadeQuad(input);
}
