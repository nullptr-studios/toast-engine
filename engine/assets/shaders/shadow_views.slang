
// Must match renderer::shadows::k_max_shadow_views
static const uint kMaxShadowViews = 228;

/// Mirrors renderer::ShadowPass::ShadowUBO and shadow_depth.slang declares only the first array of the same buffer
struct ShadowUBO
{
    float4x4 viewProjection[kMaxShadowViews];
    float4x4 inverseViewProjection[kMaxShadowViews];
};

[[vk::binding(0, 0)]]
ConstantBuffer<ShadowUBO> gShadow;
