// Mesh stage of the near cell mesh shader backend kept out of voxel.slang since its SPIR-V declares mesh shading
// One group per window of kMeshletQuads quads and one thread per slice of the window greedy merges its 8x8 faces again
import voxel;
import voxel_dda;
import voxel_mesh_common;
import voxel_mesh_window;
import lighting;

groupshared uint sScan[kMeshletQuads];

/// Clip planes a vertex lies outside of, with w behind the camera as its own bit
uint clipOutsideFlags(float4 clip)
{
    uint flags = 0u;
    flags |= clip.x < -clip.w ? 1u : 0u;
    flags |= clip.x > clip.w ? 2u : 0u;
    flags |= clip.y < -clip.w ? 4u : 0u;
    flags |= clip.y > clip.w ? 8u : 0u;
    flags |= clip.w <= 0.0 ? 16u : 0u;
    return flags;
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

    const uint2 cell = gVoxelCells[payload.cell];
    const VoxelInstance instance = gVoxelInstances[cellEntryInstance(cell.x)];

    // Every slice holds a quad so a window spans at most kMeshletQuads slices
    const uint index = sFirstEntry + thread;
    const uint entry = index < payload.entryCount ? payload.entries[index] : 0u;
    if (index < payload.entryCount && entryFirstQuad(entry) < windowStart + kMeshletQuads)
    {
        WindowSink sink = { entryFirstQuad(entry), windowStart };
        meshEntry(entry, instance.recordIndex, unpackCell(cell.y), sink);
    }
    GroupMemoryBarrierWithGroupSync();

    // A quad with all four corners outside one frustum plane covers no pixel so it never becomes a vertex
    // The survivors compact through a scan and the vertex and triangle counts shrink to them
    QuadVSOutput corners[4];
    uint packedQuad = 0u;
    bool visible = false;
    if (thread < quads)
    {
        packedQuad = windowQuad(thread);
        uint outside = 0x1Fu;
        for (uint corner = 0u; corner < 4u; ++corner)
        {
            corners[corner] = quadCorner(cell, packedQuad, corner);
            outside &= clipOutsideFlags(corners[corner].position);
        }
        visible = outside == 0u;
        // Late list only since the push constants hold no levels for the early one
        if (visible && gPush.hizLevels > 0u && quadHiddenByHiz(corners[0].position, corners[1].position, corners[2].position, corners[3].position))
        {
            visible = false;
        }
    }
    sScan[thread] = visible ? 1u : 0u;
    GroupMemoryBarrierWithGroupSync();

    // Inclusive scan over the window
    for (uint offset = 1u; offset < kMeshletQuads; offset <<= 1)
    {
        uint value = sScan[thread];
        if (thread >= offset)
        {
            value += sScan[thread - offset];
        }
        GroupMemoryBarrierWithGroupSync();
        sScan[thread] = value;
        GroupMemoryBarrierWithGroupSync();
    }
    const uint survivors = sScan[kMeshletQuads - 1u];

    SetMeshOutputCounts(survivors * 4u, survivors * 2u);
    if (visible)
    {
        const uint slot = sScan[thread] - 1u;
        const uint first = slot * 4u;
        for (uint corner = 0u; corner < 4u; ++corner)
        {
            vertices[first + corner] = corners[corner];
        }
        if (quadFlipped(instance, packedQuad))
        {
            triangles[slot * 2u] = uint3(first, first + 2u, first + 1u);
            triangles[slot * 2u + 1u] = uint3(first, first + 3u, first + 2u);
        }
        else
        {
            triangles[slot * 2u] = uint3(first, first + 1u, first + 2u);
            triangles[slot * 2u + 1u] = uint3(first, first + 2u, first + 3u);
        }
    }
}

[shader("fragment")]
[earlydepthstencil]
FSOutput meshQuadFragmentMain(QuadVSOutput input)
{
    return shadeQuad(input);
}
