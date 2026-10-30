import fullscreen;
import depth_convention;

struct MotionBlurParams
{
    // Current unjittered clip to previous clip when motion is not written
    float4x4 reprojection;
    // xy 1 over size zw size
    float4 texel;
    // x shutter share of the frame y longest blur as a share of the height z samples w 1 ignores written motion
    float4 tuning;
    // x camera near y camera far
    float4 camera;
};

[[vk::push_constant]]
ConstantBuffer<MotionBlurParams> gParams;

[[vk::binding(0, 0)]]
Sampler2D gColor;

[[vk::binding(1, 0)]]
Sampler2D gDepth;

[[vk::binding(2, 0)]]
Sampler2D gMotion;

typedef FullscreenVSOutput VSOutput;

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    return fullscreenTriangle(vertexId);
}

float noiseAt(float2 pixel)
{
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

float linearDepth(float depth)
{
#if TOAST_REVERSED_Z
    const float standardDepth = 1.0 - depth;
#else
    const float standardDepth = depth;
#endif
    const float near = gParams.camera.x;
    const float far = gParams.camera.y;
    return near * far / max(far - standardDepth * (far - near), 1e-6);
}

/// Movement of the pixel over one frame in uv
float2 velocityAt(float2 uv, float depth)
{
    const float4 motion = gMotion.SampleLevel(uv, 0.0);
    if (motion.w > 0.5 && gParams.tuning.w < 0.5)
    {
        return motion.xy;
    }
    const float4 previousClip = mul(gParams.reprojection, float4(uv * 2.0 - 1.0, depth, 1.0));
    return uv - (previousClip.xy / previousClip.w * 0.5 + 0.5);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    const float2 uv = input.uv;
    const float4 center = gColor.SampleLevel(uv, 0.0);

    const float depth = gDepth.SampleLevel(uv, 0.0).r;
    const float2 size = gParams.texel.zw;
    float2 velocity = velocityAt(uv, depth) * gParams.tuning.x;

    // Longest blur in pixels since uv is stretched along the width
    const float maxPixels = gParams.tuning.y * size.y;
    const float2 pixels = velocity * size;
    const float blurPixels = sqrt(dot(pixels, pixels));
    if (blurPixels < 1.0)
    {
        return center;
    }
    velocity *= min(1.0, maxPixels / blurPixels);

    const uint count = uint(gParams.tuning.z);
    const float centerDepth = linearDepth(depth);
    const float jitter = noiseAt(input.position.xy);

    float3 sum = center.rgb;
    float weightSum = 1.0;
    for (uint i = 0u; i < count; ++i)
    {
        const float t = (float(i) + jitter) / float(count) - 0.5;
        const float2 sampleUv = uv + velocity * t;
        const float sampleDepth = linearDepth(gDepth.SampleLevel(sampleUv, 0.0).r);

        // A sample in front of the pixel must not smear over what is behind it
        const float inFront = (centerDepth - sampleDepth) / max(centerDepth, 0.1);
        const float weight = 1.0 - smoothstep(0.02, 0.1, inFront);
        sum += gColor.SampleLevel(sampleUv, 0.0).rgb * weight;
        weightSum += weight;
    }
    return float4(sum / weightSum, center.a);
}
