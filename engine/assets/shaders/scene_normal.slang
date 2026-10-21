// World stage normal target xyz is the normal scaled by 1 + specular F0 and w the roughness SSR reads
// Readers that normalize the normal are unaffected

public float specularF0(float3 albedo, float metallic)
{
    return lerp(0.04, dot(albedo, float3(0.2126, 0.7152, 0.0722)), metallic);
}

public float4 encodeSceneNormal(float3 normal, float roughness, float f0)
{
    return float4(normalize(normal) * (1.0 + saturate(f0)), roughness);
}

public float sceneNormalF0(float4 encoded)
{
    return saturate(length(encoded.xyz) - 1.0);
}
