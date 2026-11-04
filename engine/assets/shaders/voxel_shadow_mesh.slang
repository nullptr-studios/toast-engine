// Mesh stage of the voxel shadow mesh backend which writes depth only so the pipeline has no fragment stage
import voxel_dda;
import voxel_mesh_common;
import voxel_mesh_window;
import voxel_shadow_mesh_common;
import shadow_views;

struct ShadowVertex
{
    float4 position : SV_Position;
};

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(kMeshletQuads, 1, 1)]
void shadowMeshMain(uint3 group : SV_GroupID, uint thread : SV_GroupIndex, uint viewId : SV_ViewID, in payload QuadTaskPayload payload,
                    OutputVertices<ShadowVertex, kMeshletQuads * 4> vertices, OutputIndices<uint3, kMeshletQuads * 2> triangles)
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

    const uint3 cell = unpackCell(payload.cell);
    const VoxelInstance instance = gVoxelInstances[gPush.instanceIndex];

    const uint index = sFirstEntry + thread;
    const uint entry = index < payload.entryCount ? payload.entries[index] : 0u;
    if (index < payload.entryCount && entryFirstQuad(entry) < windowStart + kMeshletQuads)
    {
        WindowSink sink = { entryFirstQuad(entry), windowStart };
        meshEntry(entry, instance.recordIndex, cell, sink);
    }
    GroupMemoryBarrierWithGroupSync();

    SetMeshOutputCounts(quads * 4u, quads * 2u);
    if (thread < quads)
    {
        const uint packedQuad = windowQuad(thread);
        const float4x4 voxelToClip = mul(gShadow.viewProjection[gPush.viewIndex + viewId], instance.voxelToWorld);
        const uint first = thread * 4u;
        for (uint corner = 0u; corner < 4u; ++corner)
        {
            vertices[first + corner].position = mul(voxelToClip, float4(quadCornerLocal(cell, packedQuad, corner), 1.0));
        }
        // Drawn without culling so the winding never matters
        triangles[thread * 2u] = uint3(first, first + 1u, first + 2u);
        triangles[thread * 2u + 1u] = uint3(first, first + 2u, first + 3u);
    }
}
