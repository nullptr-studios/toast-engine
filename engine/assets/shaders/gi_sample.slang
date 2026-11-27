// Set 0 written by SceneDescriptorSets from GiClipmapPass
import gi_common;
import gi_clip_common;

/// Published by GiClipmapPass after its inject and size.z 0 means no clipmap GI
[[vk::binding(24, 0)]]
StructuredBuffer<GiClipFrame> gGiClipFrame;

/// Face radiance at level * kGiClipFaces + face
[[vk::binding(25, 0)]]
Sampler3D gGiClipRadiance[kGiClipMaxLevels * kGiClipFaces];

/// xyz opacity along each axis and w the solid share
[[vk::binding(26, 0)]]
Sampler3D gGiClipOpacity[kGiClipMaxLevels];

/// Bounce irradiance over pi each face gathered at level * kGiClipFaces + face
[[vk::binding(27, 0)]]
Sampler3D gGiClipAmbient[kGiClipMaxLevels * kGiClipFaces];

/// Diameter per meter of a 60 degree cone
static const float kGiClipConeSpread = 1.1547;
static const uint kGiClipConeSteps = 10u;
/// 0 since finer levels leave blind rings between cones that show as halos
static const float kGiClipConeBias = 0.0;
/// Cells of level 0 the cones start off the surface so its own cells do not shade it
static const float kGiClipConeLift = 1.5;
static const float kGiClipFaceWeight = 1e-4;
/// Highest level the cones read through the B-spline filter
static const uint kGiClipSmoothLevels = 1u;

/// Premultiplied face radiance toward the ray and opacity along it where @p lod above 0 reads last level mips
float4 giClipSample(GiClipFrame frame, uint level, float lod, float3 world, float3 direction)
{
    // The repeat sampler does the toroidal wrap
    const float3 uvw = world / (frame.levels[level].w * float(frame.size.x));
    const float3 blocked = gGiClipOpacity[NonUniformResourceIndex(level)].SampleLevel(uvw, lod).xyz;
    const float3 weight = direction * direction;
    const float opacity = dot(weight, blocked);
    if (opacity <= 0.0)
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    const uint base = level * kGiClipFaces;
    float3 radiance = float3(0.0, 0.0, 0.0);
    [unroll]
    for (uint axis = 0u; axis < 3u; ++axis)
    {
        if (weight[axis] > kGiClipFaceWeight)
        {
            const uint face = base + axis * 2u + (direction[axis] > 0.0 ? 1u : 0u);
            radiance += weight[axis] * gGiClipRadiance[NonUniformResourceIndex(face)].SampleLevel(uvw, lod).rgb;
        }
    }
    // Each face is stored times its own coverage so this is already premultiplied
    return float4(radiance, opacity);
}

/// Weight and place of one of the eight filtered reads of a quadratic B-spline over the 27 cells around a point
float4 giClipSmoothTap(float3 cell, float3 f, float cells, uint tap)
{
    const float3 after = 0.5 * (0.5 + f) * (0.5 + f);
    const float3 near = 1.0 - after;
    const float3 before = 0.5 * (0.5 - f) * (0.5 - f);
    const bool3 far = bool3((tap & 1u) != 0u, (tap & 2u) != 0u, (tap & 4u) != 0u);
    const float3 share = select(far, after, near);
    const float3 at = select(far, cell + 1.5, cell - 0.5 + (near - before) / near);
    return float4(at / cells, share.x * share.y * share.z);
}

/// Rounds the creases trilinear leaves at 8 reads per texture and recomputes taps per read to save registers
float4 giClipSampleSmooth(GiClipFrame frame, uint level, float3 world, float3 direction)
{
    const float cells = float(frame.size.x);
    const float3 p = world / frame.levels[level].w;
    const float3 cell = floor(p);
    // Offset from the centre of the cell holding the point in -0.5 to 0.5
    const float3 f = p - cell - 0.5;

    float3 blocked = float3(0.0, 0.0, 0.0);
    for (uint tap = 0u; tap < 8u; ++tap)
    {
        const float4 read = giClipSmoothTap(cell, f, cells, tap);
        blocked += read.w * gGiClipOpacity[NonUniformResourceIndex(level)].SampleLevel(read.xyz, 0.0).xyz;
    }
    const float3 weight = direction * direction;
    const float opacity = dot(weight, blocked);
    if (opacity <= 0.0)
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    const uint3 face =
        level * kGiClipFaces + uint3(direction.x > 0.0 ? 1u : 0u, direction.y > 0.0 ? 3u : 2u, direction.z > 0.0 ? 5u : 4u);
    float3 radiance = float3(0.0, 0.0, 0.0);
    for (uint tap = 0u; tap < 8u; ++tap)
    {
        const float4 read = giClipSmoothTap(cell, f, cells, tap);
        [unroll]
        for (uint axis = 0u; axis < 3u; ++axis)
        {
            if (weight[axis] > kGiClipFaceWeight)
            {
                radiance += read.w * weight[axis] *
                            gGiClipRadiance[NonUniformResourceIndex(face[axis])].SampleLevel(read.xyz, 0.0).rgb;
            }
        }
    }
    return float4(radiance, opacity);
}

float giClipSolidShare(GiClipFrame frame, uint level, float3 world)
{
    const float3 uvw = world / (frame.levels[level].w * float(frame.size.x));
    return gGiClipOpacity[NonUniformResourceIndex(level)].SampleLevel(uvw, 0.0).w;
}

/// rgb radiance a coverage from @p start meters on with no level finer than @p base
float4 giClipCone<let Smooth : int>(GiClipFrame frame, float3 origin, float3 direction, float start, float base)
{
    const float cell = frame.levels[0].w;
    const float last = float(frame.size.y - 1u);
    float t = start;
    float4 sum = float4(0.0, 0.0, 0.0, 0.0);
    for (uint step = 0u; step < kGiClipConeSteps && sum.a < 0.98; ++step)
    {
        const float3 point = origin + direction * t;
        const float held = giClipLevelSoft(frame, point);
        if (held >= float(kGiClipMaxLevels))
        {
            break;
        }
        const float diameter = max(kGiClipConeSpread * t, cell);
        // Cones read cells as wide as themselves or leave blind rings so past the last level they climb its mips
        const float wanted = clamp(log2(diameter / cell) - kGiClipConeBias, max(min(held, last), base), last + float(frame.size.w));
        float4 sample;
        if (wanted > last)
        {
            sample = giClipSample(frame, uint(last), wanted - last, point, direction);
        }
        else
        {
            const uint fine = uint(floor(wanted));
            const uint coarse = min(fine + 1u, uint(last));
            const float blend = wanted - float(fine);
            // Levels 0 and 1 since the mid distance blocks show at level 1 and coarser ones are far enough to blur
            if (Smooth > 0 && fine <= kGiClipSmoothLevels && coarse != fine)
            {
                // The coarser level reaches past the 27 cells so where it holds nothing they are empty and one read settles it
                sample = float4(0.0, 0.0, 0.0, 0.0);
                if (giClipSolidShare(frame, coarse, point) > 0.0)
                {
                    sample = giClipSampleSmooth(frame, fine, point, direction);
                    if (blend > 0.0)
                    {
                        sample = lerp(sample, giClipSample(frame, coarse, 0.0, point, direction), blend);
                    }
                }
            }
            else if (blend > 0.0 && coarse != fine)
            {
                // Coarse is built from fine so where it is empty so is fine and the step costs one read
                sample = giClipSample(frame, coarse, 0.0, point, direction);
                if (sample.a > 0.0)
                {
                    sample = lerp(giClipSample(frame, fine, 0.0, point, direction), sample, blend);
                }
            }
            else
            {
                sample = giClipSample(frame, fine, 0.0, point, direction);
            }
        }
        sum += (1.0 - sum.a) * sample;
        // One cell of the level read a step so each cell is composited once with no opacity correction
        t += max(diameter, cell * exp2(wanted));
    }
    return sum;
}

float giClipCellAt(GiClipFrame frame, float3 worldPosition)
{
    return frame.levels[0].w * exp2(min(giClipLevelSoft(frame, worldPosition), float(frame.size.y - 1u)));
}

/// xyz irradiance over pi and w the share of the hemisphere the cones left open
/// @p jitter in 0 to 1 starts the cones that share of a cell further out so a history averages away where steps land
/// @p Smooth 0 reads trilinear where a history already averages away the creases the B-spline rounds off
float4 giClipGather<let Smooth : int>(GiClipFrame frame, float3 worldPosition, float3 normal, float turn, float base, float jitter)
{
    // Coarser levels thicken every surface so the cones start further off it there
    const float cell = frame.levels[0].w * exp2(base);
    const float3 origin = worldPosition + normal * (kGiClipConeLift * cell);
    const float side = normal.z >= 0.0 ? 1.0 : -1.0;
    const float a = -1.0 / (side + normal.z);
    const float b = normal.x * normal.y * a;
    const float3 tangent = float3(1.0 + side * normal.x * normal.x * a, side * b, -side * normal.x);
    const float3 bitangent = float3(b, side + normal.y * normal.y * a, -normal.y);

    const float start = jitter * cell;
    float4 cone = giClipCone<Smooth>(frame, origin, normal, start, base);
    float4 gathered = 0.25 * float4(cone.rgb, 1.0 - cone.a);
    // The smooth path keeps five side cones and the history one turns three a pixel and frame to cover as many
    const uint sides = Smooth > 0 ? 5u : 3u;
    const float spacing = 6.28318531 / float(sides);
    const float share = 0.75 / float(sides);
    for (uint i = 0u; i < sides; ++i)
    {
        const float angle = (float(i) + turn) * spacing;
        const float3 direction = normal * 0.5 + (tangent * cos(angle) + bitangent * sin(angle)) * 0.8660254;
        cone = giClipCone<Smooth>(frame, origin, direction, start, base);
        gathered += share * float4(cone.rgb, 1.0 - cone.a);
    }
    return gathered;
}

/// @p turn spins the side cones up to a fifth of a turn and lit pixels pass 0 since per frame noise flickers
/// @p denoised when a temporal history follows so one cheaper gather a pixel is enough
bool sampleGiClipmap(
    float3 worldPosition, float3 normal, float turn, out float3 irradiance, out float sky, float jitter = 0.0,
    bool denoised = false)
{
    irradiance = float3(0.0, 0.0, 0.0);
    sky = 0.0;
    const GiClipFrame frame = gGiClipFrame[0];
    if (frame.size.z == 0u)
    {
        return false;
    }
    const float soft = giClipLevelSoft(frame, worldPosition);
    if (soft >= float(kGiClipMaxLevels))
    {
        return false;
    }
    const float last = float(frame.size.y - 1u);
    const float base = floor(min(soft, last));
    const float fade = min(soft, last) - base;
    float4 gathered;
    if (denoised)
    {
        // Each pixel picks one level of a fade band with the fade as the odds and the history blends them
        const float pick = frac(turn + jitter * 0.618034);
        gathered = giClipGather<0>(frame, worldPosition, normal, turn, fade > pick ? base + 1.0 : base, jitter);
    }
    else
    {
        gathered = giClipGather<1>(frame, worldPosition, normal, turn, base, jitter);
        // Near a level edge two gathers cross fade since sliding one start between levels draws strips
        if (fade > 0.0)
        {
            gathered = lerp(gathered, giClipGather<1>(frame, worldPosition, normal, turn, base + 1.0, jitter), fade);
        }
    }
    irradiance = gathered.rgb;
    sky = gathered.a;
    return true;
}

bool sampleGiClipmapCheap(float3 worldPosition, float3 normal, out float3 irradiance, out float sky)
{
    irradiance = float3(0.0, 0.0, 0.0);
    sky = 0.0;
    const GiClipFrame frame = gGiClipFrame[0];
    if (frame.size.z == 0u)
    {
        return false;
    }
    const float soft = giClipLevelSoft(frame, worldPosition);
    if (soft >= float(kGiClipMaxLevels))
    {
        return false;
    }
    // The coarser level of a fade band alone so the band costs one cone too
    const float base = ceil(min(soft, float(frame.size.y - 1u)));
    const float3 origin = worldPosition + normal * (kGiClipConeLift * frame.levels[0].w * exp2(base));
    const float4 cone = giClipCone<0>(frame, origin, normal, 0.0, base);
    irradiance = cone.rgb;
    sky = 1.0 - cone.a;
    return true;
}

bool giIrradianceCheap(float3 worldPosition, float3 normal, out float3 irradiance, out float sky)
{
    return sampleGiClipmapCheap(worldPosition, normal, irradiance, sky);
}

/// False until the clipmap is published or off its outermost level
bool giIrradiance(float3 worldPosition, float3 normal, float turn, out float3 irradiance, out float sky)
{
    return sampleGiClipmap(worldPosition, normal, turn, irradiance, sky);
}

/// Irradiance over pi the clipmap already gathered at the faces around a surface point and 0 share off any surface
float4 giClipAmbientAt(GiClipFrame frame, uint level, float3 worldPosition, float3 normal)
{
    const float3 uvw = worldPosition / (frame.levels[level].w * float(frame.size.x));
    const float share = gGiClipOpacity[NonUniformResourceIndex(level)].SampleLevel(uvw, 0.0).w;
    const float3 weight = normal * normal;
    const uint base = level * kGiClipFaces;
    float3 irradiance = float3(0.0, 0.0, 0.0);
    [unroll]
    for (uint axis = 0u; axis < 3u; ++axis)
    {
        const uint face = base + axis * 2u + (normal[axis] > 0.0 ? 0u : 1u);
        irradiance += weight[axis] * gGiClipAmbient[NonUniformResourceIndex(face)].SampleLevel(uvw, 0.0).rgb;
    }
    return float4(irradiance, share);
}

/// Empty cells hold nothing so the filtered value is divided by the solid share around the point
/// @p blendLevels false takes the finer level alone, cheaper where a seam is hidden
bool sampleGiClipAmbient(float3 worldPosition, float3 normal, bool blendLevels, out float3 irradiance)
{
    irradiance = float3(0.0, 0.0, 0.0);
    const GiClipFrame frame = gGiClipFrame[0];
    if (frame.size.z == 0u)
    {
        return false;
    }
    const float soft = giClipLevelSoft(frame, worldPosition);
    if (soft >= float(kGiClipMaxLevels))
    {
        return false;
    }
    const float last = float(frame.size.y - 1u);
    const float level = min(soft, last);
    const uint fine = uint(floor(level));
    float4 ambient = giClipAmbientAt(frame, fine, worldPosition, normal);
    if (blendLevels && level > float(fine))
    {
        ambient = lerp(ambient, giClipAmbientAt(frame, fine + 1u, worldPosition, normal), level - float(fine));
    }
    if (ambient.a < 0.05)
    {
        return false;
    }
    irradiance = ambient.rgb / ambient.a;
    return true;
}

bool giHitIrradiance(float3 worldPosition, float3 normal, out float3 irradiance)
{
    return sampleGiClipAmbient(worldPosition, normal, true, irradiance);
}

/// For reflected hits off screen where the blur of the lobe hides the step between levels
bool giHitIrradianceNearest(float3 worldPosition, float3 normal, out float3 irradiance)
{
    return sampleGiClipAmbient(worldPosition, normal, false, irradiance);
}

/// Bounce intensity for lit surfaces only since gi_clip_bounce feeds sampleGiClipmap back into itself
float giIntensity()
{
    return gGiClipFrame[0].camera.w;
}

bool giClipCovers(float3 worldPosition)
{
    const GiClipFrame frame = gGiClipFrame[0];
    return frame.size.z != 0u && giClipLevelSoft(frame, worldPosition) < float(kGiClipMaxLevels);
}

/// Meters of contact a short ray adds below the cell the clipmap blurs at the point, valid where giClipCovers
float giContactReach(float3 worldPosition)
{
    return kGiClipConeLift * giClipCellAt(gGiClipFrame[0], worldPosition);
}

/// Each shader calls the one it needs so it compiles only that path

float4 giFull(float3 worldPosition, float3 normal)
{
    float3 irradiance;
    float sky;
    return giIrradiance(worldPosition, normal, 0.0, irradiance, sky) ? float4(irradiance, sky) : float4(0.0, 0.0, 0.0, -1.0);
}

float4 giCheap(float3 worldPosition, float3 normal)
{
    float3 irradiance;
    float sky;
    return giIrradianceCheap(worldPosition, normal, irradiance, sky) ? float4(irradiance, sky) : float4(0.0, 0.0, 0.0, -1.0);
}
