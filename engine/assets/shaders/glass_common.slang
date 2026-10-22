// Glass optics shared by voxel glass and glass debris, no resources so any pass can import it

/// Voxels of alpha 1 glass that let exactly its albedo through
static const float kGlassTintVoxels = 4.0;

/// Mirrors voxel::k_default_ior
static const float kDefaultIor = 1.5;

/// Beer Lambert through @p voxels of glass
float3 glassPathFilter(float3 albedo, float alpha, float voxels)
{
    return pow(max(albedo, float3(0.01, 0.01, 0.01)), alpha * max(voxels, 0.0) / kGlassTintVoxels);
}

/// Diffuse share of an entered surface
float glassScatter(float alpha, float roughness)
{
    return alpha * roughness;
}

/// Head on reflectance of a dielectric against air
float iorF0(float ior)
{
    const float r = (ior - 1.0) / (ior + 1.0);
    return r * r;
}

/// Palette ior byte where 0 is the default
float glassIor(uint iorByte)
{
    return iorByte == 0u ? kDefaultIor : 1.0 + float(iorByte) / 100.0;
}
