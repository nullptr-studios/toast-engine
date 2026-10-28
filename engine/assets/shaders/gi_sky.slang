// What a GI ray sees past the last cascade so the trace and the merge agree on it
import lighting;

/// Matches the lighting ambient term so turning GI on keeps the sky as bright
float3 giSky(float3 direction)
{
    const float3 ambient = gCamera.ambientColorIntensity.rgb * gCamera.ambientColorIntensity.a;
    return gCamera.environmentParams.x >= 0.5 ? ambient + gPrefilteredEnv.SampleLevel(direction, 0.0).rgb : ambient;
}
