// Forward glass one box per glass brick far to near
import voxel_dda;
import lighting;
import scene_normal;
import depth_convention;

/// Instance index then brick grid index sorted far to near
[[vk::binding(7, 1)]]
StructuredBuffer<uint2> gGlassBricks;

/// Mirrors renderer::VoxelPass::GlassPush
struct GlassPush
{
    /// Run start in gGlassBricks since SV_InstanceID leaves out firstInstance
    uint first;
    /// 0 when the backdrop was not captured this frame
    float refraction;
    /// Meters past which glass does not refract
    float refractionDistance;
    /// Voxels the glass run walk follows
    uint refractionVoxels;
    /// Lit glass surfaces per brick and pixel past which layers only absorb
    uint litSurfaces;
};

[[vk::push_constant]]
GlassPush gGlassPush;

/// Scene colour and depth after the sky and before anything blended
[[vk::binding(8, 1)]]
Sampler2D gBackdrop;

[[vk::binding(9, 1)]]
Sampler2D gBackdropDepth;

/// Ratio blending reads a near black backdrop as a difference instead
static const float kRatioFloor = 0.02;
static const float kMaxRatio = 16.0;


/// Below this nothing behind shows
static const float kOpaqueTransmittance = 0.004;


struct VSOutput
{
    float4 position : SV_Position;

    [[vk::location(0)]]
    float3 worldPosition : TEXCOORD0;

    [[vk::location(1)]]
    nointerpolation uint instanceIndex : TEXCOORD1;

    [[vk::location(2)]]
    nointerpolation int3 brick : TEXCOORD2;

    // Flat per draw or linear in position so fragments skip the instance fetch
    [[vk::location(3)]]
    nointerpolation uint volume : TEXCOORD3;

    [[vk::location(4)]]
    nointerpolation uint brickEntry : TEXCOORD4;

    [[vk::location(5)]]
    nointerpolation float3 voxelCamera : TEXCOORD5;

    [[vk::location(6)]]
    float3 voxelPosition : TEXCOORD6;

    [[vk::location(7)]]
    nointerpolation float nearOrigin : TEXCOORD7;

    [[vk::location(8)]]
    float nearStep : TEXCOORD8;
};

/// Back faces so a camera inside the brick still covers it
[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const uint2 entry = gGlassBricks[gGlassPush.first + instanceId];
    const VoxelInstance instance = gVoxelInstances[entry.x];
    const VolumeRecord record = gVolumeRecords[instance.recordIndex];
    const uint3 brick = uint3(entry.y % record.brickDimsX, (entry.y / record.brickDimsX) % record.brickDimsY,
                              entry.y / (record.brickDimsX * record.brickDimsY));

    uint corner = vertexId % 3u;
    if ((instance.flags & kInstanceMirrored) != 0u && corner != 0u)
    {
        corner = 3u - corner;
    }
    const float3 local = float3(brick * kBrickDim) + kBoxCorners[kBoxIndices[vertexId - (vertexId % 3u) + corner]] * float(kBrickDim);
    const float4 world = mul(instance.voxelToWorld, float4(local, 1.0));

    VSOutput output;
    output.worldPosition = world.xyz;
    output.position = mul(gCamera.jitteredViewProjection, world);
    output.instanceIndex = entry.x;
    output.brick = int3(brick);
    output.volume = instance.recordIndex;
    output.brickEntry = gridEntry(record, brick);
    output.voxelCamera = mul(instance.worldToVoxel, float4(gCamera.position, 1.0)).xyz;
    output.voxelPosition = local;
    output.nearOrigin = clipNearDistance(mul(gCamera.viewProjection, float4(gCamera.position, 1.0)));
    output.nearStep = clipNearDistance(mul(gCamera.viewProjection, float4(world.xyz - gCamera.position, 0.0)));
    return output;
}

struct GlassComposite
{
    /// Premultiplied by the transmittance in front
    float3 color;
    /// Transmittance of the colour behind
    float3 transmittance;
    /// Of the nearest glass in the brick
    float depth;
    bool debugView;

    /// Nearest entered surface for SSR and TAA
    bool entered;
    float3 normal;
    float roughness;
    float f0;
    float3 voxelPosition;

    /// Backdrop uv the refracted ray lands on
    bool refracted;
    float2 refractedUv;
};

float3 shadeInterface(float3 hitWorld, float3 normal, float3 view, float2 pixel, PaletteSample palette, out float fresnel,
                      out bool debugView)
{
    Surface surface;
    surface.marchedShadows = false;
    surface.worldPosition = hitWorld;
    surface.normal = normal;
    surface.geometricNormal = normal;
    surface.viewDirection = view;
    surface.albedo = palette.albedo;
    // Dielectric with the specular lobe always on so the IOR alone sets reflectance
    surface.metallic = 0.0;
    surface.reflectivity = 1.0;
    surface.roughness = clamp(palette.roughness, 0.045, 1.0);
    surface.occlusion = 1.0;
    surface.viewSpaceZ = mul(gCamera.view, float4(hitWorld, 1.0)).z;
    surface.pixel = pixel;

    const float f0 = iorF0(palette.ior);
    const float facing = pow(1.0 - saturate(dot(normal, view)), 5.0);
    fresnel = f0 + (1.0 - f0) * facing;

    // One pass split into lobes since each pass resamples every shadow map
    const ShadingResult lit = shadeSurfaceLighting(surface);
    debugView = lit.isDebugView;
    if (debugView)
    {
        return lit.color;
    }
    const float3 diffuse = max(lit.color - lit.specular, float3(0.0, 0.0, 0.0));
    // Lighting assumes F0 0.04 so rescale by the Fresnel ratio
    const float3 specular = lit.specular * fresnel / (0.04 + 0.96 * facing);
    return diffuse * glassScatter(palette.alpha, palette.roughness) + palette.albedo * palette.emissive * palette.alpha + specular;
}


/// First glass run of the camera ray across the whole volume
struct GlassRun
{
    float entryT;
    float exitT;
    int3 entryVoxel;
    float3 entryNormal;
    float3 exitNormal;
    float ior;
};

float3 voxelFaceNormal(float4x4 worldToVoxel, int axis, float sign)
{
    float3 voxelNormal = float3(0.0, 0.0, 0.0);
    voxelNormal[axis] = sign;
    return normalize(mul(transpose((float3x3)worldToVoxel), voxelNormal));
}

/// False when an opaque voxel comes first or the camera starts inside glass
bool firstGlassRun(uint volume, VolumeRecord record, float4x4 worldToVoxel, float3 origin, float3 direction, float tMin,
                   out GlassRun run)
{
    run.entryT = 0.0;
    run.exitT = 0.0;
    run.entryVoxel = int3(0, 0, 0);
    run.entryNormal = float3(0.0, 0.0, 0.0);
    run.exitNormal = float3(0.0, 0.0, 0.0);
    run.ior = kDefaultIor;

    const int3 dims = int3(record.brickDimsX, record.brickDimsY, record.brickDimsZ) * int(kBrickDim);
    const RayHit hit = marchRayBox(volume, origin, direction, tMin, kFloatMax, int3(0, 0, 0), dims, false);
    if (!hit.hit || hit.axis < 0 || !glassEntry(record, hit.material))
    {
        return false;
    }
    run.entryT = hit.t;
    run.entryVoxel = hit.voxel;
    run.entryNormal = voxelFaceNormal(worldToVoxel, hit.axis, direction[hit.axis] > 0.0 ? -1.0 : 1.0);
    run.ior = samplePalette(record, hit.material).ior;

    int3 voxel = hit.voxel;
    int3 stepSign;
    float3 next;
    float3 delta;
    for (int a = 0; a < 3; ++a)
    {
        stepSign[a] = direction[a] > 0.0 ? 1 : (direction[a] < 0.0 ? -1 : 0);
        delta[a] = stepSign[a] == 0 ? kFloatMax : abs(1.0 / direction[a]);
        next[a] = stepSign[a] == 0 ? kFloatMax : (float(voxel[a] + (stepSign[a] > 0 ? 1 : 0)) - origin[a]) / direction[a];
    }

    // Parallel faces when the run is longer than the walk
    run.exitT = hit.t;
    run.exitNormal = -run.entryNormal;
    int3 cachedBrick = int3(-1, -1, -1);
    uint cachedEntry = 0u;
    uint cachedMaterial = hit.material;
    bool cachedGlass = true;
    for (uint step = 0u; step < gGlassPush.refractionVoxels; ++step)
    {
        const int axis = next.x < next.y ? (next.x < next.z ? 0 : 2) : (next.y < next.z ? 1 : 2);
        run.exitT = next[axis];
        voxel[axis] += stepSign[axis];
        next[axis] += delta[axis];
        bool glass = false;
        if (all(voxel >= int3(0, 0, 0)) && all(voxel < dims))
        {
            const int3 brick = voxel >> 3;
            if (any(brick != cachedBrick))
            {
                cachedBrick = brick;
                cachedEntry = gridEntry(record, uint3(brick));
            }
            const uint material = brickMaterialAt(cachedEntry, voxel);
            if (material != cachedMaterial)
            {
                cachedMaterial = material;
                cachedGlass = material != 0u && glassEntry(record, material);
            }
            glass = cachedGlass;
        }
        if (!glass)
        {
            run.exitNormal = voxelFaceNormal(worldToVoxel, axis, float(stepSign[axis]));
            break;
        }
    }
    return true;
}

/// Camera distance of a backdrop texel
float backdropDistance(float2 uv, float depth)
{
    const float4 viewPosition = mul(gClusterParams.inverseProjection, float4(uv * 2.0 - 1.0, depth, 1.0));
    return length(viewPosition.xyz / viewPosition.w);
}

/// Snell at entry and exit faces with a straight path inside
/// The bent ray goes as far past the glass as the straight one
float2 refractedBackdropUv(float3 camera, float3 worldDirection, float entryT, float exitT, float3 entryNormal, float3 exitNormal,
                           float ior, float2 pixel, out bool valid)
{
    valid = false;
    float2 size;
    gBackdrop.GetDimensions(size.x, size.y);
    const float2 straightUv = pixel / size;

    const float rayLength = length(worldDirection);
    const float3 direction = worldDirection / rayLength;
    const float3 entryPoint = camera + worldDirection * entryT;

    const float3 inside = refract(direction, entryNormal, 1.0 / ior);
    if (dot(inside, inside) < 1e-6)
    {
        return straightUv;
    }
    const float across = (exitT - entryT) * rayLength * max(dot(direction, -entryNormal), 1e-3);
    const float3 exitPoint = entryPoint + inside * (across / max(dot(inside, -entryNormal), 1e-3));
    float3 bent = refract(inside, -exitNormal, ior);
    // Total internal reflection keeps the inside direction
    if (dot(bent, bent) < 1e-6)
    {
        bent = inside;
    }

    const float straightDepth = gBackdropDepth.Load(int3(int2(pixel), 0)).r;
    const float exitDistance = distance(camera, exitPoint);
    const float remaining = isFarDepth(straightDepth) ? 1000.0 : max(backdropDistance(straightUv, straightDepth) - exitDistance, 0.0);
    const float4 clip = mul(gCamera.jitteredViewProjection, float4(exitPoint + bent * remaining, 1.0));
    if (clip.w <= 1e-4)
    {
        return straightUv;
    }
    const float2 uv = lerp(straightUv, clip.xy / clip.w * 0.5 + 0.5, gGlassPush.refraction);
    if (any(uv < float2(0.0, 0.0)) || any(uv > float2(1.0, 1.0)))
    {
        return straightUv;
    }

    // Something in front of the glass never shows through it
    const float bentDepth = gBackdropDepth.SampleLevel(uv, 0.0).r;
    if (!isFarDepth(bentDepth) && backdropDistance(uv, bentDepth) < distance(camera, entryPoint))
    {
        return straightUv;
    }
    valid = true;
    return uv;
}

/// Every glass layer front to back or with @p surfaceOnly the nearest entered surface unlit
/// Surfaces reflect by Fresnel and scatter by glassScatter and the path filters by glassPathFilter
/// Never shortened per voxel or the grid shows
bool compositeGlass(VSOutput input, bool surfaceOnly, out GlassComposite result)
{
    result.color = float3(0.0, 0.0, 0.0);
    result.transmittance = float3(1.0, 1.0, 1.0);
    result.depth = input.position.z;
    result.debugView = false;
    result.entered = false;
    result.normal = float3(0.0, 0.0, 0.0);
    result.roughness = 1.0;
    result.f0 = 0.0;
    result.voxelPosition = float3(0.0, 0.0, 0.0);
    result.refracted = false;
    result.refractedUv = float2(0.0, 0.0);
    if (isVoxelDebugMode(gCamera.renderModePad.x))
    {
        return false;
    }

    const uint volume = input.volume;
    const VolumeRecord record = gVolumeRecords[volume];
    // Only surface normals and refraction read it
    const float4x4 worldToVoxel = gVoxelInstances[input.instanceIndex].worldToVoxel;
    const float3 camera = gCamera.position;

    // t = 1 is the back face
    const float3 worldDirection = input.worldPosition - camera;
    const float3 origin = input.voxelCamera;
    const float3 direction = input.voxelPosition - input.voxelCamera;
    const float3 view = -normalize(worldDirection);

    const float tMin = input.nearStep > 0.0 ? max(0.0, -input.nearOrigin / input.nearStep) : 0.0;

    const int3 boxMin = input.brick * int(kBrickDim);
    const int3 boxMax = boxMin + int(kBrickDim);

    float t = tMin;
    float tExit = kFloatMax;
    int axis = -1;
    float3 inverse = float3(kFloatMax, kFloatMax, kFloatMax);
    int3 stepSign = int3(0, 0, 0);
    for (int a = 0; a < 3; ++a)
    {
        if (direction[a] == 0.0)
        {
            if (origin[a] < float(boxMin[a]) || origin[a] > float(boxMax[a]))
            {
                return false;
            }
            continue;
        }
        inverse[a] = 1.0 / direction[a];
        stepSign[a] = direction[a] > 0.0 ? 1 : -1;
        const float t0 = (float(boxMin[a]) - origin[a]) * inverse[a];
        const float t1 = (float(boxMax[a]) - origin[a]) * inverse[a];
        if (min(t0, t1) > t)
        {
            t = min(t0, t1);
            axis = a;
        }
        tExit = min(tExit, max(t0, t1));
    }
    if (t > tExit)
    {
        return false;
    }

    int3 voxel = int3(floor(origin + direction * t));
    if (axis >= 0)
    {
        voxel[axis] = stepSign[axis] > 0 ? boxMin[axis] : boxMax[axis] - 1;
    }
    voxel = clamp(voxel, boxMin, boxMax - 1);

    // Glass continuing from the voxel the ray came through was entered in another brick or holds the camera
    int3 previous = voxel;
    if (axis >= 0)
    {
        previous[axis] -= stepSign[axis];
    }
    bool inside = glassEntry(record, voxelMaterialAt(record, previous));

    float3 next;
    for (int a = 0; a < 3; ++a)
    {
        next[a] = stepSign[a] == 0 ? kFloatMax : (float(voxel[a] + (stepSign[a] > 0 ? 1 : 0)) - origin[a]) * inverse[a];
    }
    const float3 delta = abs(inverse);
    const float voxelsPerT = length(direction);

    bool touched = false;
    float firstT = t;
    uint litInterfaces = 0u;

    // A run of glass is nearly always one palette entry
    uint cachedMaterial = 0xFFFFFFFFu;
    bool cachedGlass = false;
    PaletteSample palette = {};

    for (uint step = 0u; step < 3u * kBrickDim; ++step)
    {
        // The march stays in its brick so one grid entry covers it
        const uint material = brickMaterialAt(input.brickEntry, voxel);
        const int nextAxis = next.x < next.y ? (next.x < next.z ? 0 : 2) : (next.y < next.z ? 1 : 2);
        const float tNext = min(next[nextAxis], tExit);

        if (material != 0u)
        {
            // The opaque surface behind is already in the scene colour the transmittance filters
            if (material != cachedMaterial)
            {
                cachedMaterial = material;
                cachedGlass = glassEntry(record, material);
                if (cachedGlass)
                {
                    palette = samplePalette(record, material);
                }
            }
            if (!cachedGlass)
            {
                break;
            }

            const float thickness = max(tNext - t, 0.0) * voxelsPerT;
            if (!touched)
            {
                touched = true;
                firstT = t;
            }

            if (!inside && axis >= 0)
            {
                float3 voxelNormal = float3(0.0, 0.0, 0.0);
                voxelNormal[axis] = direction[axis] > 0.0 ? -1.0 : 1.0;
                const float3 normal = normalize(mul(transpose((float3x3)worldToVoxel), voxelNormal));

                if (!result.entered)
                {
                    result.entered = true;
                    result.normal = normal;
                    result.roughness = clamp(palette.roughness, 0.045, 1.0);
                    result.f0 = iorF0(palette.ior);
                    result.voxelPosition = origin + direction * t;
                    if (surfaceOnly)
                    {
                        firstT = t;
                        break;
                    }
                }

                float fresnel = 0.0;
                if (litInterfaces < gGlassPush.litSurfaces)
                {
                    bool debugView = false;
                    const float3 light = shadeInterface(camera + worldDirection * t, normal, view, input.position.xy, palette, fresnel,
                                                        debugView);
                    if (debugView)
                    {
                        result.color = light;
                        result.transmittance = float3(0.0, 0.0, 0.0);
                        result.debugView = true;
                        firstT = t;
                        break;
                    }
                    result.color += result.transmittance * light;
                    ++litInterfaces;
                }

                // What the surface reflects or scatters does not pass through
                result.transmittance *= (1.0 - fresnel) * (1.0 - glassScatter(palette.alpha, palette.roughness));
            }
            else if (surfaceOnly)
            {
                // Glass continuing from a nearer brick whose own surface pass writes the surface
                break;
            }

            result.transmittance *= glassPathFilter(palette.albedo, palette.alpha, thickness);

            if (max(result.transmittance.x, max(result.transmittance.y, result.transmittance.z)) < kOpaqueTransmittance)
            {
                result.transmittance = float3(0.0, 0.0, 0.0);
                break;
            }
        }
        inside = material != 0u;

        if (next[nextAxis] > tExit)
        {
            break;
        }
        axis = nextAxis;
        t = next[nextAxis];
        voxel[nextAxis] += stepSign[nextAxis];
        next[nextAxis] += delta[nextAxis];
        // A plane tied with tExit steps out where brickEntry would wrap
        if (voxel[nextAxis] < boxMin[nextAxis] || voxel[nextAxis] >= boxMax[nextAxis])
        {
            break;
        }
    }

    if (!touched || (surfaceOnly && !result.entered))
    {
        return false;
    }
    const float4 clip = mul(gCamera.viewProjection, float4(camera + worldDirection * firstT, 1.0));
    result.depth = clip.z / clip.w;

    // Only the brick holding the entry voxel refracts
    GlassRun run;
    if (!surfaceOnly && !result.debugView && result.entered && gGlassPush.refraction > 0.0 &&
        firstT * length(worldDirection) <= gGlassPush.refractionDistance &&
        firstGlassRun(volume, record, worldToVoxel, origin, direction, tMin, run) && all((run.entryVoxel >> 3) == input.brick))
    {
        result.refractedUv = refractedBackdropUv(camera, worldDirection, run.entryT, run.exitT, run.entryNormal, run.exitNormal, run.ior,
                                                 input.position.xy, result.refracted);
    }
    return true;
}

// Location 0 index 1 is the second blend source so the background is tinted not just faded
struct DualSourceOutput
{
    [[vk::location(0)]]
    [[vk::index(0)]]
    float4 color : SV_Target0;

    [[vk::location(0)]]
    [[vk::index(1)]]
    float4 transmittance : SV_Target1;
};

[shader("fragment")]
DualSourceOutput fragmentMain(VSOutput input, out float depth : SV_Depth)
{
    GlassComposite glass;
    if (!compositeGlass(input, false, glass))
    {
        discard;
    }
    depth = glass.depth;

    // The blend sees only the straight backdrop so the bent one scales or offsets it
    float3 offset = float3(0.0, 0.0, 0.0);
    float3 scale = glass.transmittance;
    if (glass.refracted)
    {
        const float3 straight = gBackdrop.Load(int3(int2(input.position.xy), 0)).rgb;
        const float3 bent = gBackdrop.SampleLevel(glass.refractedUv, 0.0).rgb;
        [unroll]
        for (int c = 0; c < 3; ++c)
        {
            if (straight[c] > kRatioFloor)
            {
                scale[c] *= min(bent[c] / straight[c], kMaxRatio);
            }
            else
            {
                offset[c] = scale[c] * (bent[c] - straight[c]);
            }
        }
    }

    DualSourceOutput output;
    output.color = float4(glass.color + offset, 1.0);
    output.transmittance = float4(scale, 1.0);
    return output;
}

/// Without dualSrcBlend the tint is lost and the background only fades
[shader("fragment")]
float4 fragmentPremultiplied(VSOutput input, out float depth : SV_Depth) : SV_Target0
{
    GlassComposite glass;
    if (!compositeGlass(input, false, glass))
    {
        discard;
    }
    depth = glass.depth;
    return float4(glass.color, 1.0 - dot(glass.transmittance, float3(1.0, 1.0, 1.0)) / 3.0);
}

// Mirrors the world stage attachment order with the scene colour masked off
struct SurfaceOutput
{
    float4 color : SV_Target0;
    float4 normalRoughness : SV_Target1;
    /// Zero so SSAO leaves glass and what shows through it alone
    float4 indirectDiffuse : SV_Target2;
    /// xy uv motion z reactive w 1 when written
    float4 motion : SV_Target3;
};

/// After every blended draw
[shader("fragment")]
SurfaceOutput fragmentSurface(VSOutput input, out float depth : SV_Depth)
{
    GlassComposite glass;
    if (!compositeGlass(input, true, glass))
    {
        discard;
    }
    depth = glass.depth;

    const VoxelInstance instance = gVoxelInstances[input.instanceIndex];
    SurfaceOutput output;
    output.color = float4(0.0, 0.0, 0.0, 0.0);
    output.normalRoughness = encodeSceneNormal(glass.normal, glass.roughness, glass.f0);
    output.indirectDiffuse = float4(0.0, 0.0, 0.0, 1.0);
    output.motion = float4(0.0, 0.0, 0.0, 0.0);
    if ((instance.flags & kInstanceNoMotion) == 0u)
    {
        const float4 clip = mul(gCamera.viewProjection, mul(instance.voxelToWorld, float4(glass.voxelPosition, 1.0)));
        const float4 previousClip = mul(gCamera.previousViewProjection, mul(instance.previousVoxelToWorld, float4(glass.voxelPosition, 1.0)));
        output.motion = float4((clip.xy / clip.w - previousClip.xy / previousClip.w) * 0.5, 0.0, 1.0);
    }
    return output;
}
