// Gives every texel a motion vector so the temporal resolve and upscalers never need their own depth reprojection
// Written motion is kept and the rest is rebuilt from depth

/// Mirrors renderer::MotionFill::Push
struct MotionFillParams
{
    // Current unjittered clip to previous clip
    float4x4 reprojection;
    uint2 size;
    // 1 rebuilds every texel since written motion is stale after skipped frames
    uint ignoreWritten;
};

[[vk::push_constant]]
ConstantBuffer<MotionFillParams> gParams;

[[vk::binding(0, 0)]]
Sampler2D gDepth;

[[vk::binding(1, 0)]]
Sampler2D gMotion;

/// xy current minus previous uv, z reactive, w 1 when the scene wrote it
[[vk::binding(2, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> gFilledMotion;

[[vk::binding(3, 0)]]
[[vk::image_format("r8")]]
RWTexture2D<float> gReactive;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gParams.size))
    {
        return;
    }

    const float2 uv = (float2(id.xy) + 0.5) / float2(gParams.size);
    float4 motion = gMotion.SampleLevel(uv, 0.0);

    if (motion.w < 0.5 || gParams.ignoreWritten != 0u)
    {
        const float depth = gDepth.SampleLevel(uv, 0.0).r;
        const float4 previousClip = mul(gParams.reprojection, float4(uv * 2.0 - 1.0, depth, 1.0));
        motion.xy = uv - (previousClip.xy / previousClip.w * 0.5 + 0.5);
    }

    gFilledMotion[id.xy] = motion;
    gReactive[id.xy] = saturate(motion.z);
}
