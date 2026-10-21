// Shared by the voxel shadow task and mesh stages
import shadow_views;

/// Mirrors renderer::ShadowPass::VoxelMeshShadowPush
struct ShadowMeshPush
{
    uint instanceIndex;
    /// The first view of the layer group
    uint viewIndex;
    uint viewCount;
    /// Nonzero for a sun whose views are orthographic
    uint directional;
};

[[vk::push_constant]]
ShadowMeshPush gPush;
