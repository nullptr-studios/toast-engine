
static const uint kGiClipMaxLevels = 6u;
/// Mirrors renderer::GiClipmapPass::k_faces
static const uint kGiClipFaces = 6u;

/// Mirrors renderer::GiClipmapPass::FrameData
struct GiClipFrame
{
    float4x4 inverseViewProjection;
    float4 camera;
    /// xyz lowest global cell w meters per cell
    float4 levels[kGiClipMaxLevels];
    /// x resolution y level count z 1 once lighting may read it w mips above the last level
    uint4 size;
};

int3 giClipWrap(int3 cell, int resolution)
{
    // Unsigned since drivers get a signed remainder of a negative cell wrong unless the resolution is a power of two
    return int3(uint3(cell + resolution * 65536) % uint(resolution));
}

/// Finest level whose box holds the point with one cell of margin or kGiClipMaxLevels outside all
uint giClipLevelAt(GiClipFrame frame, float3 world)
{
    const int resolution = int(frame.size.x);
    for (uint level = 0u; level < frame.size.y; ++level)
    {
        const float4 box = frame.levels[level];
        const int3 cell = int3(floor(world / box.w)) - int3(box.xyz);
        if (all(cell >= 1) && all(cell < resolution - 1))
        {
            return level;
        }
    }
    return kGiClipMaxLevels;
}

static const float kGiClipFade = 8.0;

/// Measured from the camera not the snapped box so the fade glides
float giClipLevelSoft(GiClipFrame frame, float3 world)
{
    // The box centre trails the camera by up to two cells and one more keeps the filter footprint inside
    const float reach = 0.5 * float(frame.size.x) - 3.0;
    const float3 offset = abs(world - frame.camera.xyz);
    const float distance = max(offset.x, max(offset.y, offset.z));
    for (uint level = 0u; level < frame.size.y; ++level)
    {
        const float inside = reach - distance / frame.levels[level].w;
        if (inside >= 0.0)
        {
            const bool last = level + 1u >= frame.size.y;
            return float(level) + (last ? 0.0 : saturate(1.0 - inside / kGiClipFade));
        }
    }
    return float(kGiClipMaxLevels);
}

bool giClipInner(GiClipFrame frame, uint level, int3 cell)
{
    if (level == 0u)
    {
        return false;
    }
    // Level origins are even so the finer box lands on whole cells of this level
    const int3 low = int3(frame.levels[level - 1u].xyz) >> 1;
    return all(cell >= low) && all(cell < low + int(frame.size.x) / 2);
}

/// Outward normal with even faces positive and the axis in face >> 1
int3 giClipFace(uint face)
{
    const int sign = (face & 1u) == 0u ? 1 : -1;
    const uint axis = face >> 1;
    return int3(axis == 0u ? sign : 0, axis == 1u ? sign : 0, axis == 2u ? sign : 0);
}

/// Unsigned float of 5 exponent bits and @p bits of mantissa with tiny values flushed to 0
uint giClipPackFloat(float value, uint bits)
{
    const uint raw = asuint(max(value, 0.0));
    const int exponent = int((raw >> 23) & 0xFFu) - 112;
    if (exponent <= 0)
    {
        return 0u;
    }
    const uint mask = (1u << bits) - 1u;
    if (exponent >= 31)
    {
        return (30u << bits) | mask;
    }
    return (uint(exponent) << bits) | ((raw >> (23u - bits)) & mask);
}

/// B10G11R11 bits so a float view of the same texels filters them
uint giClipPackRadiance(float3 radiance)
{
    return giClipPackFloat(radiance.r, 6u) | (giClipPackFloat(radiance.g, 6u) << 11) | (giClipPackFloat(radiance.b, 5u) << 22);
}

float3 giClipCellCentre(GiClipFrame frame, uint level, int3 cell)
{
    return (float3(cell) + 0.5) * frame.levels[level].w;
}
