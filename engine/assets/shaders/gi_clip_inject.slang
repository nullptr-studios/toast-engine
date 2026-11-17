import lighting;
import gi_clip_common;
import gi_clip_trace;

[[vk::binding(0, 1)]]
StructuredBuffer<GiClipFrame> gClipFrame;

[[vk::binding(2, 1)]]
Sampler3D gEmission;

/// One per giClipFace of the level written and sampled as B10G11R11 through a second view
[[vk::binding(3, 1)]]
[[vk::image_format("r32ui")]]
RWTexture3D<uint> gRadiance[kGiClipFaces];

/// Bounce irradiance over pi per giClipFace of the level written
[[vk::binding(4, 1)]]
Sampler3D gAmbient[kGiClipFaces];

/// xyz share of the cell blocked along each axis of the level written
[[vk::binding(5, 1)]]
Sampler3D gOpacity;

/// Mirrors gi_clip_fill GiClipFillPush
struct GiClipInjectPush
{
    /// xyz first global cell w level
    int4 boxMin;
    int4 boxSize;
};

[[vk::push_constant]]
GiClipInjectPush gPush;

static const uint kGiClipMaxLocalLights = 64u;

float clipFaceCosines(uint exposed, float3 L)
{
    float cosines = 0.0;
    for (uint face = 0u; face < kGiClipFaces; ++face)
    {
        if ((exposed & (1u << face)) != 0u)
        {
            cosines += max(dot(float3(giClipFace(face)), L), 0.0);
        }
    }
    return cosines;
}

struct ClipFaces
{
    float3 irradiance[kGiClipFaces];
};

void clipAddLight(inout ClipFaces faces, uint exposed, float3 L, float3 radiance)
{
    for (uint face = 0u; face < kGiClipFaces; ++face)
    {
        if ((exposed & (1u << face)) != 0u)
        {
            faces.irradiance[face] += radiance * max(dot(float3(giClipFace(face)), L), 0.0);
        }
    }
}

/// Centre of the open neighbour on the face most turned to the light and half a cell toward it
/// Starting off the face and not along the light keeps grazing rays out of the cells beside it which would shadow the
/// whole ring past some angle and make it jump a cell as the light moves
float3 clipLightOrigin(uint exposed, float3 centre, float3 L, float cellSize)
{
    float best = -2.0;
    float3 normal = L;
    for (uint face = 0u; face < kGiClipFaces; ++face)
    {
        const float3 direction = float3(giClipFace(face));
        const float facing = dot(direction, L);
        if ((exposed & (1u << face)) != 0u && facing > best)
        {
            best = facing;
            normal = direction;
        }
    }
    return centre + normal * cellSize + L * (0.5 * cellSize);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void injectMain(uint3 id : SV_DispatchThreadID)
{
    if (any(int3(id) >= gPush.boxSize.xyz))
    {
        return;
    }
    const GiClipFrame frame = gClipFrame[0];
    const uint level = uint(gPush.boxMin.w);
    const int resolution = int(frame.size.x);
    const float cellSize = frame.levels[level].w;
    const int3 low = int3(frame.levels[level].xyz);
    const int3 global = gPush.boxMin.xyz + int3(id);
    if (giClipInner(frame, level, global))
    {
        return;
    }
    const int3 texel = giClipWrap(global, resolution);

    const float4 material = gClipMaterial[level].Load(int4(texel, 0));
    // A neighbour outside the level counts as solid so the level edge never lights up
    uint exposed = 0u;
    for (uint face = 0u; face < kGiClipFaces && material.a > 0.0; ++face)
    {
        const int3 neighbour = global + giClipFace(face);
        const int3 local = neighbour - low;
        if (any(local < 0) || any(local >= resolution))
        {
            continue;
        }
        if (gClipMaterial[level].Load(int4(giClipWrap(neighbour, resolution), 0)).a <= 0.0)
        {
            exposed |= 1u << face;
        }
    }
    if (exposed == 0u)
    {
        for (uint face = 0u; face < kGiClipFaces; ++face)
        {
            gRadiance[face][texel] = 0u;
        }
        return;
    }

    const float3 emission = gEmission.Load(int4(texel, 0)).rgb;
    const float3 centre = giClipCellCentre(frame, level, global);
    // Kept clear of the lamp end so the cells holding it do not block it
    const float lift = 1.5 * cellSize;
    const float sunReach = 0.5 * float(resolution) * frame.levels[frame.size.y - 1u].w;
    ClipFaces faces;
    for (uint face = 0u; face < kGiClipFaces; ++face)
    {
        faces.irradiance[face] = float3(0.0, 0.0, 0.0);
    }

    for (uint di = 0u; di < gCamera.directionalLightCountPad.x; ++di)
    {
        const DirectionalLightGpu light = gCamera.directionalLights[di];
        const float3 L = normalize(-light.direction.xyz);
        if (clipFaceCosines(exposed, L) <= 0.0)
        {
            continue;
        }
        const float visibility = giClipVisibility(frame, clipLightOrigin(exposed, centre, L, cellSize), L * sunReach);
        clipAddLight(faces, exposed, L, light.colorIntensity.rgb * (light.colorIntensity.a * visibility));
    }

    const uint lightCount = min(gClusterParams.lightCounts.x, kGiClipMaxLocalLights);
    for (uint li = 0u; li < lightCount; ++li)
    {
        const GpuLight light = gLights[li];
        float3 L;
        float distance;
        float3 radiance = lightSampleRadiance(light, centre, light.worldPosRange.xyz, L, distance);
        if (all(radiance <= 0.0) || clipFaceCosines(exposed, L) <= 0.0)
        {
            continue;
        }
        // The cell stands for a whole cell of surface so the inverse square stops at a cell or cells next to the lamp
        // swing by orders of magnitude with where it sits inside its cell and the bounce flares with them
        const float nearest = max(distance, cellSize);
        radiance *= (distance * distance) / (nearest * nearest);
        // Stops short of the lamp so the cells holding it do not block it
        const float3 origin = clipLightOrigin(exposed, centre, L, cellSize);
        const float3 toLight = light.worldPosRange.xyz - origin;
        const float reach = max(length(toLight) - 2.0 * lift, 0.0);
        const float visibility = reach > 0.0 ? giClipVisibility(frame, origin, normalize(toLight) * reach) : 1.0;
        clipAddLight(faces, exposed, L, radiance * visibility);
    }

    // Premultiplied by the covered share of the cell so filtering and downsampling keep its energy
    const float3 blocked = gOpacity.Load(int4(texel, 0)).xyz;
    for (uint face = 0u; face < kGiClipFaces; ++face)
    {
        const bool open = (exposed & (1u << face)) != 0u;
        const float3 bounce = gAmbient[face].Load(int4(texel, 0)).rgb;
        const float3 radiance = emission + material.rgb * (faces.irradiance[face] / kPi + bounce);
        gRadiance[face][texel] = open ? giClipPackRadiance(radiance * blocked[face >> 1]) : 0u;
    }
}
