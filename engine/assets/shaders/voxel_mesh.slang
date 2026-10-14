// Mesh stage of the near cell mesh shader backend kept out of voxel.slang since its SPIR-V declares mesh shading
// One group per meshlet greedy merges the masked slices of one brick into at most 64 quads
import voxel;
import voxel_dda;
import voxel_mesh_common;
import lighting;

groupshared uint sOccupancy[16];
/// 16 words per direction loaded only for the sides the mask touches
groupshared uint sNeighbours[6 * 16];
groupshared uint sQuads[kMeshletQuads];
groupshared uint sQuadCount;

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(64, 1, 1)]
void quadMeshMain(uint3 group : SV_GroupID, uint thread : SV_GroupIndex, in payload QuadTaskPayload payload,
                  OutputVertices<QuadVSOutput, kMeshletQuads * 4> vertices, OutputIndices<uint3, kMeshletQuads * 2> triangles)
{
    const uint2 meshlet = payload.meshlets[group.x];
    const uint brickInCell = meshlet.x & 63u;
    const uint2 mask = uint2(meshlet.y, meshlet.x >> 6);

    const uint2 cell = gVoxelCells[payload.entryIndex];
    const VoxelInstance instance = gVoxelInstances[cellEntryInstance(cell.x)];
    const int3 brick = int3(brickOfCell(cell, brickInCell));

    // Words 0 to 15 are the brick and the rest the neighbour of each masked side
    for (uint index = thread; index < 16u * 7u; index += 64u)
    {
        if (index < 16u)
        {
            sOccupancy[index] = occupancyWord(brickEntryAt(instance.recordIndex, brick), index);
            continue;
        }
        const uint direction = (index - 16u) >> 4;
        const uint bits = direction < 4u ? (mask.x >> (direction * 8u)) & 0xFFu : (mask.y >> ((direction - 4u) * 8u)) & 0xFFu;
        if (bits != 0u)
        {
            const uint neighbourEntry = brickEntryAt(instance.recordIndex, brick + directionStep(direction));
            sNeighbours[index - 16u] = occupancyWord(neighbourEntry, (index - 16u) & 15u);
        }
    }
    if (thread == 0u)
    {
        sQuadCount = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    // One thread per masked slice greedy merges its 8x8 faces
    const bool masked = thread < kBrickSlices && ((thread < 32u ? mask.x >> thread : mask.y >> (thread - 32u)) & 1u) != 0u;
    if (masked)
    {
        const uint direction = thread >> 3;
        const uint slice = thread & 7u;
        const uint axis = direction >> 1;
        const bool positive = (direction & 1u) == 0u;

        uint occupancy[16];
        uint neighbour[16];
        for (uint word = 0u; word < 16u; ++word)
        {
            occupancy[word] = sOccupancy[word];
            neighbour[word] = sNeighbours[direction * 16u + word];
        }

        uint faces[16];
        exposedFaces(occupancy, neighbour, direction, faces);
        uint rows[8];
        sliceRows(faces, axis, slice, rows);
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

                uint index;
                InterlockedAdd(sQuadCount, 1u, index);
                sQuads[index] = packQuad(direction, slice, u, v, width, height, brickInCell);
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    const uint quads = sQuadCount;
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
FSOutput meshQuadFragmentMain(QuadVSOutput input)
{
    FSOutput output;
    if (!shadeQuad(input, output))
    {
        discard;
    }
    return output;
}
