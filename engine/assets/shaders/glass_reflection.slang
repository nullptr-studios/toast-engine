// Glass reflections against the opaque backdrop shared by voxel glass and glass debris
// The SSR and traced specular post passes read the opaque surface behind glass so glass reflects in its own shading
import lighting;
import depth_convention;

/// View depth of an NDC depth whatever the projection handedness
float glassViewDepth(float ndcDepth)
{
    const float a = gCamera.projection[2][2];
    const float b = gCamera.projection[2][3];
    const float c = gCamera.projection[3][2];
    return c * b / (ndcDepth * c - a);
}

/// Backdrop colour where @p position shows on screen and false when something else covers it
bool glassBackdropRadiance(Sampler2D backdrop, Sampler2D backdropDepth, float3 position, out float3 radiance)
{
    radiance = float3(0.0, 0.0, 0.0);
    const float4 clip = mul(gCamera.jitteredViewProjection, float4(position, 1.0));
    if (clip.w <= 0.0 || any(abs(clip.xy / clip.w) >= 1.0))
    {
        return false;
    }
    const float2 uv = clip.xy / clip.w * 0.5 + 0.5;
    const float depth = backdropDepth.SampleLevel(uv, 0.0).r;
    if (isFarDepth(depth) || abs(glassViewDepth(depth) - clip.w) > 0.05 + 0.02 * clip.w)
    {
        return false;
    }
    radiance = backdrop.SampleLevel(uv, 0.0).rgb;
    return true;
}

/// Linear march against the backdrop depth
// shortcut: no HiZ or edge fade, upgrade if glass reflections shimmer or cut off at screen edges
bool glassScreenReflection(Sampler2D backdrop, Sampler2D backdropDepth, float3 position, float3 direction, float reach, uint steps,
                           float thickness, out float3 radiance)
{
    radiance = float3(0.0, 0.0, 0.0);
    if (steps == 0u)
    {
        return false;
    }
    const float stride = reach / float(steps);
    for (uint step = 1u; step <= steps; ++step)
    {
        const float4 clip = mul(gCamera.jitteredViewProjection, float4(position + direction * (stride * float(step)), 1.0));
        if (clip.w <= 0.0 || any(abs(clip.xy / clip.w) >= 1.0))
        {
            return false;
        }
        const float2 uv = clip.xy / clip.w * 0.5 + 0.5;
        const float depth = backdropDepth.SampleLevel(uv, 0.0).r;
        if (isFarDepth(depth))
        {
            continue;
        }
        const float behind = clip.w - glassViewDepth(depth);
        if (behind > 0.0)
        {
            if (behind > thickness + stride)
            {
                return false;
            }
            radiance = backdrop.SampleLevel(uv, 0.0).rgb;
            return true;
        }
    }
    return false;
}

/// Specular a reflection carries weighted as lighting weights the F0 0.04 environment it replaces
float3 glassReflectionSpecular(float3 litSpecular, float3 ambientSpecular, float3 reflection, float3 normal, float3 view, float roughness)
{
    const float NdotV = saturate(dot(normal, view));
    const float2 split = environmentBRDF(roughness, NdotV);
    const float3 weight = fresnelSchlickRoughness(NdotV, float3(0.04, 0.04, 0.04), roughness) * split.x + split.y;
    return litSpecular + reflection * weight - ambientSpecular;
}
