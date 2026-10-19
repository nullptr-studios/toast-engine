// Quad window shared by the camera and shadow mesh stages and never imported by a task stage
import voxel_mesh_common;

groupshared uint sFirstEntry;
groupshared uint sQuads[kMeshletQuads];

uint windowQuad(uint index)
{
    return sQuads[index];
}

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
