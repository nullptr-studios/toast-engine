// Hi-Z screen march shared by SSR and the traced voxel specular
import depth_convention;

/// What a march reads besides the depth and its closest depth renderer::HiZPyramid
public struct ScreenMarch
{
    public float4x4 viewProjection;
    public float4x4 inverseViewProjection;
    /// Meters the ray may travel before the screen edge clips it
    public float reach;
    /// Meters behind a surface a ray still counts as hitting it, scaled with depth
    public float thickness;
    public uint maxSteps;
    /// Levels of the pyramid
    public int levels;
};

/// Pixels a finest level step scans along the ray so grazing floors reach as far as steep walls
static const float kScanCell = 8.0;

/// Clip w from the 1 / w row of the inverse view projection
float marchLinearDepth(ScreenMarch march, float2 uv, float depth)
{
    return 1.0 / max(dot(march.inverseViewProjection[3], float4(uv * 2.0 - 1.0, depth, 1.0)), 1e-8);
}

/// Full resolution check of the ray against gDepth at @p t, @p behind true when the ray went behind a surface too thick to hit
bool pixelHit(ScreenMarch march, Sampler2D depthTexture, float2 p0, float2 d, float z0, float dz, float t, float2 size,
              out bool behind)
{
    behind = false;
    const float2 uv = (p0 + d * t) / size;
    const float depth = depthTexture.SampleLevel(uv, 0.0).r;
    if (isFarDepth(depth) || isCloserDepth(z0 + dz * t, depth))
    {
        return false;
    }
    const float sceneDepth = marchLinearDepth(march, uv, depth);
    const float gap = marchLinearDepth(march, uv, z0 + dz * t) - sceneDepth;
    behind = gap > march.thickness * (1.0 + 0.025 * sceneDepth);
    return !behind;
}

/// Hi-Z march in screen space clipped to the screen and the reach so a ray goes as far as the screen shows
/// @p fade drops reach limited rays near their end so they fade out instead of cutting off
public bool marchScreenHiz(ScreenMarch march, Sampler2D depthTexture, Sampler2D hiz, float3 origin, float3 direction, float jitter,
                           out float2 hitUv, out float fade)
{
    hitUv = float2(0.0, 0.0);
    fade = 1.0;

    uint width;
    uint height;
    depthTexture.GetDimensions(width, height);
    const float2 size = float2(float(width), float(height));

    float reach = march.reach;
    const float4 start = mul(march.viewProjection, float4(origin, 1.0));
    float4 end = mul(march.viewProjection, float4(origin + direction * reach, 1.0));

    // Stops short of the near plane so 1 / w stays finite
    const float minW = 1e-3;
    if (end.w < minW)
    {
        reach *= (start.w - minW) / max(start.w - end.w, 1e-6);
        end = mul(march.viewProjection, float4(origin + direction * reach, 1.0));
    }

    const float2 p0 = (start.xy / start.w * 0.5 + 0.5) * size;
    const float2 p1 = (end.xy / end.w * 0.5 + 0.5) * size;
    const float z0 = start.z / start.w;
    const float dz = end.z / end.w - z0;
    const float2 d = p1 - p0;
    const float pixels = length(d);
    if (pixels < 1.0)
    {
        return false;
    }

    // Clipped to the screen edge so the reach is only a cap
    float tEnd = 1.0;
    [unroll]
    for (int axis = 0; axis < 2; ++axis)
    {
        if (abs(d[axis]) > 1e-6)
        {
            tEnd = min(tEnd, ((d[axis] > 0.0 ? size[axis] - 0.5 : 0.5) - p0[axis]) / d[axis]);
        }
    }
    if (tEnd <= 0.0)
    {
        return false;
    }
    const bool reachLimited = tEnd >= 1.0;

    const float2 inverseD = float2(abs(d.x) > 1e-6 ? 1.0 / d.x : 1e30, abs(d.y) > 1e-6 ? 1.0 / d.y : 1e30);
    const float2 forward = step(float2(0.0, 0.0), d);
    const float tPixel = 1.0 / pixels;
    const int topLevel = max(march.levels - 1, 0);
    const uint maxSteps = max(march.maxSteps, 1u);

    float t = tPixel * (1.0 + jitter);
    int level = 0;
    for (uint i = 0u; i < maxSteps && t < tEnd; ++i)
    {
        const float2 p = p0 + d * t;
        const float cellSize = float(2u << uint(level));
        const float2 cell = floor(p / cellSize);
        const float cellDepth = hiz.Load(int3(int2(cell), level)).r;
        const float2 exits = ((cell + forward) * cellSize - p0) * inverseD;
        // A hair past the boundary so the next cell is the one the ray enters
        const float tCell = min(min(exits.x, exits.y) + 0.01 * tPixel, tEnd);

        if (!isCloserDepth(z0 + dz * t, cellDepth))
        {
            if (level > 0)
            {
                --level;
                continue;
            }
            // The 2x2 cell holds something at least as close as the ray so test pixels at full resolution
            // on to the edge of the 8x8 cell around it since a grazing ray otherwise spends a step every 2 pixels
            const float2 scanCell = floor(p / kScanCell);
            const float2 scanExits = ((scanCell + forward) * kScanCell - p0) * inverseD;
            const float tScan = max(min(min(scanExits.x, scanExits.y) + 0.01 * tPixel, tEnd), tCell);
            float previous = t - tPixel;
            for (float x = t; x < tScan; x += tPixel)
            {
                bool behind;
                if (pixelHit(march, depthTexture, p0, d, z0, dz, x, size, behind))
                {
                    float lo = max(previous, 0.0);
                    float hi = x;
                    [unroll]
                    for (uint j = 0u; j < 4u; ++j)
                    {
                        const float mid = 0.5 * (lo + hi);
                        const float2 midUv = (p0 + d * mid) / size;
                        const float midDepth = depthTexture.SampleLevel(midUv, 0.0).r;
                        if (!isFarDepth(midDepth) && !isCloserDepth(z0 + dz * mid, midDepth))
                        {
                            hi = mid;
                        }
                        else
                        {
                            lo = mid;
                        }
                    }
                    hitUv = (p0 + d * hi) / size;
                    fade = reachLimited ? 1.0 - smoothstep(0.7, 1.0, hi) : 1.0;
                    return true;
                }
                previous = x;
            }
            t = tScan;
            continue;
        }

        if (isCloserDepth(z0 + dz * tCell, cellDepth))
        {
            // Wholly in front of everything in the cell
            t = tCell;
            level = min(level + 1, topLevel);
        }
        else
        {
            // Reaches the closest depth inside the cell so refine from there
            t = max(t + 0.01 * tPixel, (cellDepth - z0) / dz);
            level = max(level - 1, 0);
        }
    }
    return false;
}
