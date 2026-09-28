// Mirrors renderer::k_voxel_albedo_format alpha
public static const float kGbufferLit = 1.0;
public static const float kGbufferUnlit = 0.5;

public bool isVoxelCoverage(float albedoAlpha)
{
    return albedoAlpha > 0.25;
}

public bool isLitVoxel(float albedoAlpha)
{
    return albedoAlpha > 0.75;
}

// Emission stops a material byte can encode
static const float kEmissiveStops = 16.0;

public float encodeEmissive(float emissive)
{
    return saturate(log2(1.0 + max(emissive, 0.0)) / kEmissiveStops);
}

public float decodeEmissive(float encoded)
{
    return exp2(encoded * kEmissiveStops) - 1.0;
}
