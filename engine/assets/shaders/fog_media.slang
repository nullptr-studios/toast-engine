// Fog media shared by the volumetrics pass and GI rays
import lighting;

// Mirrors VulkanRenderer::FogVolumeGpu
struct FogVolume
{
    float4x4 worldToLocal;
    /// xyz half extents w edge fade in meters
    float4 extentsFade;
    /// rgb albedo a extinction per meter
    float4 albedoDensity;
    /// rgb radiance per meter at density 1 a noise strength
    float4 emissionNoise;
    /// xyz wind in meters per second w noise scale in meters
    float4 windScale;
    /// x anisotropy y ambient scale
    float4 phase;
    /// xyz box centre in world space
    float4 centre;
};

[[vk::binding(22, 0)]]
StructuredBuffer<FogVolume> gFogVolumes;

uint fogVolumeCount()
{
    return uint(gCamera.fogMedium.w);
}

/// Height fog extinction along origin + direction * t integrated over [near, far]
float heightFogDepth(float3 origin, float3 direction, float near, float far)
{
    const float density = gCamera.fogMedium.x;
    if (density <= 0.0 || far <= near)
    {
        return 0.0;
    }
    const float falloff = gCamera.fogMedium.z;
    const float startHeight = origin.y + direction.y * near - gCamera.fogMedium.y;
    const float atStart = density * exp(-clamp(falloff * startHeight, -20.0, 20.0));
    const float slope = falloff * direction.y;
    const float span = far - near;
    return abs(slope) < 1e-4 ? atStart * span : atStart * (1.0 - exp(-clamp(slope * span, -20.0, 20.0))) / slope;
}

/// Volumes count as uniform boxes since noise averages out and the edge fade is thin
float fogOpticalDepth(float3 origin, float3 direction, float near, float far)
{
    float depth = heightFogDepth(origin, direction, near, far);
    const uint count = fogVolumeCount();
    for (uint v = 0u; v < count; ++v)
    {
        const FogVolume volume = gFogVolumes[v];
        const float3 localOrigin = mul(volume.worldToLocal, float4(origin, 1.0)).xyz;
        const float3 localDirection = mul(volume.worldToLocal, float4(direction, 0.0)).xyz;
        const float3 inverse = 1.0 / select(abs(localDirection) > 1e-8, localDirection, float3(1e-8, 1e-8, 1e-8));
        const float3 t0 = (-volume.extentsFade.xyz - localOrigin) * inverse;
        const float3 t1 = (volume.extentsFade.xyz - localOrigin) * inverse;
        const float3 low = min(t0, t1);
        const float3 high = max(t0, t1);
        const float enter = max(max(low.x, low.y), max(low.z, near));
        const float exit = min(min(high.x, high.y), min(high.z, far));
        depth += max(exit - enter, 0.0) * volume.albedoDensity.a;
    }
    return depth;
}

float fogTransmittance(float3 origin, float3 direction, float near, float far)
{
    return exp(-fogOpticalDepth(origin, direction, near, far));
}
