// Forward glass one box per glass brick far to near
import voxel_dda;
import lighting;
import gi_sample;
import gi_common;
import scene_trace;
import trace_shading;
import scene_normal;
import glass_reflection;
import fullscreen;
import depth_convention;

/// Instance index then brick grid index sorted far to near
[[vk::binding(7, 1)]]
StructuredBuffer<uint2> gGlassBricks;

/// Mirrors renderer::VoxelPass::GlassPush
struct GlassPush
{
    /// Run start in gGlassBricks since SV_InstanceID leaves out firstInstance
    uint first;
    /// Lit glass surfaces per brick and pixel past which layers only absorb
    uint litSurfaces;
    /// 1 when gBackdropDepth holds this frame opaque depth and the pipeline keeps only glass in front of it
    uint depthTest;
    /// Meters a reflection ray travels
    float reflectionReach;
    /// Rougher surfaces keep the environment
    float reflectionRoughness;
    /// Screen march steps
    uint reflectionSteps;
    /// Meters behind the opaque depth a screen march still hits
    float reflectionThickness;
    /// Meters a traced hit sun ray travels before it counts as lit
    float sunReach;
    /// 1 draws only bricks holding the camera with back faces and 0 only the rest with front faces
    uint inside;
    /// 1 walks the run near to far so depth writing passes reject what is behind early
    uint reverse;
    /// Bricks in the run so reverse can index it
    uint count;
    /// 1 when gNearestGlassDepth holds this frame nearest glass surface
    uint nearestValid;
    /// 1 adds a cost heat colour instead of the glass, see blendGlass
    uint costView;
};

/// Bit of the gGlassBricks instance word set for a brick the camera is inside
static const uint kGlassInsideBit = 0x80000000u;

static const uint kReflectionNone = 0u;
/// One voxel ray since the traced specular post pass reads the opaque surface behind the glass
static const uint kReflectionTraced = 1u;
/// Screen march against the opaque backdrop since the ssr post pass reads the opaque surface behind the glass
static const uint kReflectionScreen = 2u;

/// Mirrors renderer::VoxelPass glass variants: what replaces the environment specular of the first surface per brick
/// Specialized per pipeline so the driver drops the reflection paths a variant never takes and the shader keeps fewer registers
[[vk::constant_id(0)]]
const uint kSpecReflection = 0u;
/// 0 drops the debug views from the variants that draw a normal frame
[[vk::constant_id(1)]]
const uint kSpecDebugViews = 1u;

/// Lit surfaces one brick fragment records, the most glass_lit_surfaces allows
static const uint kMaxLitSurfaces = 4u;
/// Below this share of its light reaching the camera a surface is left unlit
static const float kContributionCutoff = 0.02;
/// Below this share of reflected light reaching the camera a surface keeps the environment instead of tracing
static const float kReflectionCutoff = 0.01;

[[vk::push_constant]]
GlassPush gGlassPush;

/// Scene colour and depth after the sky and before anything blended
[[vk::binding(8, 1)]]
Sampler2D gBackdrop;

[[vk::binding(9, 1)]]
Sampler2D gBackdropDepth;

/// Nearest glass surface depth over the opaque depth from the glass pre-pass
[[vk::binding(16, 1)]]
Sampler2D gNearestGlassDepth;

/// Scene normal and motion of that surface so the surface copy skips the brick walk
[[vk::binding(17, 1)]]
Sampler2D gNearestGlassNormal;

[[vk::binding(18, 1)]]
Sampler2D gNearestGlassMotion;

/// r glass covers the pixel g share of light the nearest glass surface lets through
[[vk::binding(19, 1)]]
Sampler2D gGlassMask;

/// Below this the front pane is a mirror and glass behind it is skipped
static const float kMirrorTransmittance = 0.02;

/// The DDA hit is never closer than the front face so outside bricks keep early depth testing
#if TOAST_REVERSED_Z
#define GLASS_FARTHER_DEPTH SV_DepthLessEqual
#else
#define GLASS_FARTHER_DEPTH SV_DepthGreaterEqual
#endif



/// Below this nothing behind shows
static const float kOpaqueTransmittance = 0.004;

static const float kVoxelSize = 0.1;
/// Voxels off the glass face a traced reflection starts
static const float kReflectionOffset = 0.6;


struct VSOutput
{
    float4 position : SV_Position;

    [[vk::location(0)]]
    float3 worldPosition : TEXCOORD0;

    // Few interpolants since attribute storage, not registers alone, limited how many glass warps launched
    [[vk::location(1)]]
    nointerpolation uint instanceIndex : TEXCOORD1;

    /// Brick grid index in its volume
    [[vk::location(2)]]
    nointerpolation uint brickIndex : TEXCOORD2;

    [[vk::location(3)]]
    nointerpolation uint brickEntry : TEXCOORD3;
};

/// Back faces so a camera inside the brick still covers it
[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const uint run = gGlassPush.reverse != 0u ? gGlassPush.count - 1u - instanceId : instanceId;
    const uint2 packed = gGlassBricks[gGlassPush.first + run];
    const uint2 entry = uint2(packed.x & ~kGlassInsideBit, packed.y);
    if (((packed.x & kGlassInsideBit) != 0u) != (gGlassPush.inside != 0u))
    {
        // Zero area so the other pipeline of this run draws it
        VSOutput culled = (VSOutput)0;
        culled.position = float4(0.0, 0.0, 0.0, 1.0);
        return culled;
    }
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
    output.brickIndex = entry.y;
    output.brickEntry = gridEntry(record, brick);
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

    /// Share of light the entered surface lets through past its reflection and scatter
    float frontTransmittance;

    /// Surfaces lit and whether one traced or marched a reflection, for the cost view
    uint shaded;
    bool reflected;
};

/// Radiance a voxel ray hits, from the backdrop when it shows the hit and otherwise shaded as voxel_specular hitRadiance shades it
bool tracedReflection(float3 position, float3 normal, float3 direction, float2 pixel, out float3 radiance)
{
    radiance = float3(0.0, 0.0, 0.0);
    const float3 origin = position + normal * (kReflectionOffset * kVoxelSize);
    // A pixel wide cone so a far hit marches coarser levels than the mirror ray would
    float2 size;
    gBackdrop.GetDimensions(size.x, size.y);
    const float pixelSlope = 2.0 / max(abs(gCamera.projection[1][1]) * size.y, 1.0);
    const SceneHit hit = traceScene(origin, direction, 0.0, gGlassPush.reflectionReach, pixelSlope);
    if (!hit.hit)
    {
        // Grazing rays mostly run past every voxel or into meshes, which the backdrop still shows, so the environment is the last resort
        return gGlassPush.depthTest != 0u && glassScreenReflection(gBackdrop, gBackdropDepth, origin, direction, gGlassPush.reflectionReach,
                                                                   gGlassPush.reflectionSteps, gGlassPush.reflectionThickness, radiance);
    }
    const float3 hitPosition = origin + direction * hit.t;
    const float3 hitNormal = sceneHitNormal(hit, direction);
    if (gGlassPush.depthTest != 0u && dot(hitNormal, gCamera.position - hitPosition) > 0.0 &&
        glassBackdropRadiance(gBackdrop, gBackdropDepth, hitPosition, radiance))
    {
        return true;
    }
    // Lit per hit like voxel_specular hitRadiance since clipmap cells span several voxels and read as a blur beside the backdrop hits
    const PaletteSample hitPalette = samplePalette(gTraceVolumes[hit.volume].recordIndex, hit.material);
    float3 irradiance;
    if (!giHitIrradiance(hitPosition, hitNormal, irradiance))
    {
        irradiance = gCamera.ambientColorIntensity.rgb * gCamera.ambientColorIntensity.a;
        if (gCamera.environmentParams.x >= 0.5)
        {
            irradiance += gIrradianceMap.SampleLevel(hitNormal, 0.0).rgb;
        }
    }
    uint random = giHash(uint(pixel.x) | (uint(pixel.y) << 16));
    radiance = hitPalette.albedo * (hitPalette.emissive + irradiance) +
               traceLocalLight(hitPosition, hitNormal, hitPalette.albedo, pixelSlope, random) +
               traceSunLight(hitPosition, hitNormal, hitPalette.albedo, pixelSlope, gGlassPush.sunReach, pixel);
    return true;
}

/// SSR or traced radiance along the mirror direction and false keeps the environment
bool glassReflection(float3 position, float3 normal, float3 view, float roughness, float2 pixel, out float3 radiance)
{
    radiance = float3(0.0, 0.0, 0.0);
    if (kSpecReflection == kReflectionNone || roughness > gGlassPush.reflectionRoughness)
    {
        return false;
    }
    // shortcut: mirror ray only so glass below the roughness cutoff reflects too sharp, upgrade to a GGX lobe sample with TAA
    const float3 direction = reflect(-view, normal);
    if (kSpecReflection == kReflectionTraced)
    {
        return tracedReflection(position, normal, direction, pixel, radiance);
    }
    return gGlassPush.depthTest != 0u && glassScreenReflection(gBackdrop, gBackdropDepth, position, direction, gGlassPush.reflectionReach,
                                                               gGlassPush.reflectionSteps, gGlassPush.reflectionThickness, radiance);
}

/// @p reflects swaps the environment specular for glassReflection where it hits
/// @p clustered false leaves out the cluster lights so a pane behind the nearest one only takes the suns and ambient
float3 shadeInterface(float3 hitWorld, float3 normal, float3 view, float2 pixel, PaletteSample palette, bool reflects,
                      bool clustered, out float fresnel, out bool debugView)
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

    // Glass scatters a sliver of its diffuse light and a pixel can cross several panes
    gGiGathered = giCheap(hitWorld, normal);
    // One pass split into lobes since each pass resamples every shadow map
    // Every layer gets every light or glass seen through glass looks unlit
    ShadingResult lit;
    if (kSpecDebugViews != 0u)
    {
        lit = shadeSurfaceLighting(surface);
    }
    else
    {
        NoShadowFilter none;
        ClusterCell cluster = clusterCellAt(surface.viewSpaceZ, surface.pixel);
        if (!clustered)
        {
            cluster.lightCount = 0u;
        }
        lit = evaluateLighting(surface, cluster, none);
    }
    debugView = lit.isDebugView;
    if (debugView)
    {
        return lit.color;
    }
    const float3 diffuse = max(lit.color - lit.specular, float3(0.0, 0.0, 0.0));
    float3 litSpecular = lit.specular;
    float3 reflection;
    if (reflects && glassReflection(hitWorld, normal, view, surface.roughness, pixel, reflection))
    {
        litSpecular = glassReflectionSpecular(lit.specular, lit.ambientSpecular, reflection, normal, view, surface.roughness);
    }
    // Lighting assumes F0 0.04 so rescale by the Fresnel ratio
    const float3 specular = litSpecular * fresnel / (0.04 + 0.96 * facing);
    return diffuse * glassScatter(palette.alpha, palette.roughness) + palette.albedo * palette.emissive * palette.alpha + specular;
}


/// Every glass layer front to back, or with SurfaceOnly 1 the nearest entered surface unlit
/// SurfaceOnly is a template so the attachment-writing passes never reference the nearest mask and depth samplers
/// Surfaces reflect by Fresnel and scatter by glassScatter and the path filters by glassPathFilter
/// Never shortened per voxel or the grid shows
bool compositeGlass<let SurfaceOnly : int>(VSOutput input, out GlassComposite result)
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
    result.frontTransmittance = 1.0;
    result.shaded = 0u;
    result.reflected = false;
    if (kSpecDebugViews != 0u && isVoxelDebugMode(gCamera.renderModePad.x))
    {
        return false;
    }

    const VoxelInstance instance = gVoxelInstances[input.instanceIndex];
    const VolumeRecord record = gVolumeRecords[instance.recordIndex];
    const float4x4 worldToVoxel = instance.worldToVoxel;
    const float3 camera = gCamera.position;

    // t = 1 is the rasterized face
    const float3 worldDirection = input.worldPosition - camera;
    const float3 origin = mul(worldToVoxel, float4(camera, 1.0)).xyz;
    const float3 direction = mul(worldToVoxel, float4(worldDirection, 0.0)).xyz;
    const float3 view = -normalize(worldDirection);

    const float nearOrigin = clipNearDistance(mul(gCamera.viewProjection, float4(camera, 1.0)));
    const float nearStep = clipNearDistance(mul(gCamera.viewProjection, float4(worldDirection, 0.0)));
    const float tMin = nearStep > 0.0 ? max(0.0, -nearOrigin / nearStep) : 0.0;

    const uint3 brick = uint3(input.brickIndex % record.brickDimsX, (input.brickIndex / record.brickDimsX) % record.brickDimsY,
                              input.brickIndex / (record.brickDimsX * record.brickDimsY));
    const int3 boxMin = int3(brick) * int(kBrickDim);
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

    // Surfaces the walk entered within the lit budget, shaded after it so the walk loop stays small
    // Written by constant index so they stay in registers
    float slotT[kMaxLitSurfaces];
    /// Material with the axis in the top two bits
    uint slotKey[kMaxLitSurfaces];
    float3 slotWeight[kMaxLitSurfaces];
    // Only the first surface can reflect so only its fresnel is kept
    float firstFresnel = 0.0;
    [unroll]
    for (uint k = 0u; k < kMaxLitSurfaces; ++k)
    {
        slotT[k] = 0.0;
        slotKey[k] = 0u;
        slotWeight[k] = float3(0.0, 0.0, 0.0);
    }

    // A run of glass is nearly always one palette entry
    uint cachedMaterial = 0xFFFFFFFFu;
    bool cachedGlass = false;
    PaletteSample palette = {};

    const bool pooled = (input.brickEntry & 3u) >= kTagShared;
    for (uint step = 0u; step < 3u * kBrickDim; ++step)
    {
        // Empty 4x4x4 sub-cells of a pooled brick are crossed in one step, which leaves a thin pane only a few steps
        if (pooled)
        {
            const uint3 local = uint3(voxel & 7);
            const uint subBase = ((input.brickEntry >> 2) << 4) + (((local.x >> 2) | ((local.y >> 2) << 1) | ((local.z >> 2) << 2)) << 1);
            if ((gBrickOccupancy[subBase] | gBrickOccupancy[subBase + 1u]) == 0u)
            {
                const int3 low = voxel & int3(~3, ~3, ~3);
                float tLeave = kFloatMax;
                int leaveAxis = 0;
                for (int a = 0; a < 3; ++a)
                {
                    if (stepSign[a] != 0)
                    {
                        const float bound = float(low[a] + (stepSign[a] > 0 ? 4 : 0));
                        const float ta = (bound - origin[a]) * inverse[a];
                        if (ta < tLeave)
                        {
                            tLeave = ta;
                            leaveAxis = a;
                        }
                    }
                }
                if (tLeave >= tExit)
                {
                    break;
                }
                inside = false;
                t = tLeave;
                axis = leaveAxis;
                voxel = clamp(int3(floor(origin + direction * t)), low, low + 3);
                voxel[leaveAxis] = stepSign[leaveAxis] > 0 ? low[leaveAxis] + 4 : low[leaveAxis] - 1;
                if (any(voxel < boxMin) || any(voxel >= boxMax))
                {
                    break;
                }
                for (int a = 0; a < 3; ++a)
                {
                    next[a] = stepSign[a] == 0 ? kFloatMax : (float(voxel[a] + (stepSign[a] > 0 ? 1 : 0)) - origin[a]) * inverse[a];
                }
                continue;
            }
        }

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
                // Depth is written so the depth test runs after shading and hidden glass would be lit for nothing
                if (gGlassPush.depthTest != 0u)
                {
                    const float4 entry = mul(gCamera.viewProjection, float4(camera + worldDirection * t, 1.0));
                    const float entryDepth = entry.z / entry.w;
                    const float opaque = gBackdropDepth.Load(int3(int2(input.position.xy), 0)).r;
                    // A margin so anything the real test might keep still reaches it
                    if (isCloserDepth(opaque, entryDepth) && abs(opaque - entryDepth) > 1e-5)
                    {
                        return false;
                    }
                }
                // Behind a front pane that reflects nearly everything nothing of this brick shows, so its lit layers would only add light
                if (SurfaceOnly == 0 && gGlassPush.nearestValid != 0u)
                {
                    const int3 texel = int3(int2(input.position.xy), 0);
                    const float2 mask = gGlassMask.Load(texel).rg;
                    if (mask.r > 0.5 && mask.g < kMirrorTransmittance)
                    {
                        const float entryW = mul(gCamera.viewProjection, float4(camera + worldDirection * t, 1.0)).w;
                        if (glassViewDepth(gNearestGlassDepth.Load(texel).r) < entryW - (0.01 + 0.002 * entryW))
                        {
                            return false;
                        }
                    }
                }
            }

            if (!inside && axis >= 0)
            {
                float3 voxelNormal = float3(0.0, 0.0, 0.0);
                voxelNormal[axis] = direction[axis] > 0.0 ? -1.0 : 1.0;
                const float3 normal = normalize(mul(transpose((float3x3)worldToVoxel), voxelNormal));
                // Every surface reflects whether or not it is lit, or past the lit budget glass would turn clear and show the lit layers behind
                const float f0 = iorF0(palette.ior);
                float fresnel = f0 + (1.0 - f0) * pow(1.0 - saturate(dot(normal, view)), 5.0);

                if (!result.entered)
                {
                    result.entered = true;
                    result.frontTransmittance = (1.0 - fresnel) * (1.0 - glassScatter(palette.alpha, palette.roughness));
                    result.normal = normal;
                    result.roughness = clamp(palette.roughness, 0.045, 1.0);
                    result.f0 = iorF0(palette.ior);
                    result.voxelPosition = origin + direction * t;
                    if (SurfaceOnly != 0)
                    {
                        firstT = t;
                        break;
                    }
                }

                // Within the budget since every lit layer adds light and stacked glass would wash out
                if (litInterfaces < min(gGlassPush.litSurfaces, kMaxLitSurfaces))
                {
                    [unroll]
                    for (uint k = 0u; k < kMaxLitSurfaces; ++k)
                    {
                        if (k == litInterfaces)
                        {
                            slotT[k] = t;
                            slotKey[k] = material | (uint(axis) << 30u);
                            slotWeight[k] = result.transmittance;
                            if (k == 0u)
                            {
                                firstFresnel = fresnel;
                            }
                        }
                    }
                    ++litInterfaces;
                }

                // What the surface reflects or scatters does not pass through
                result.transmittance *= (1.0 - fresnel) * (1.0 - glassScatter(palette.alpha, palette.roughness));
            }
            else if (SurfaceOnly != 0)
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

    if (!touched || (SurfaceOnly != 0 && !result.entered))
    {
        return false;
    }
    const float4 clip = mul(gCamera.viewProjection, float4(camera + worldDirection * firstT, 1.0));
    result.depth = clip.z / clip.w;

    // Only the nearest glass surface of the pixel takes the cluster lights and the reflection, the panes behind it are seen through
    // it so they keep the suns and ambient alone. Without the pre-pass the first surface of each brick stands in. Debug views keep
    // every surface on one path so the first one still shows
    const float3 nearWeight = slotWeight[0];
    bool nearFirst = kSpecDebugViews == 0u && litInterfaces > 0u && max(nearWeight.x, max(nearWeight.y, nearWeight.z)) >= kContributionCutoff;
    if (SurfaceOnly == 0 && nearFirst && gGlassPush.nearestValid != 0u)
    {
        const float slotW = mul(gCamera.viewProjection, float4(camera + worldDirection * slotT[0], 1.0)).w;
        const float nearestW = glassViewDepth(gNearestGlassDepth.Load(int3(int2(input.position.xy), 0)).r);
        nearFirst = slotW <= nearestW + (0.01 + 0.002 * slotW);
    }
    const float nearT = slotT[0];
    const uint nearKey = slotKey[0];

    // Layers behind the first in a brick that face the same way with the same palette entry lie under a brick apart so they share one lighting
    bool lightCached = false;
    int lightAxis = -1;
    uint lightMaterial = 0u;
    float3 cachedLight = float3(0.0, 0.0, 0.0);
    // A real loop so the lighting is inlined once, and the nearest surface after it once the slot arrays are dead so the cluster
    // lights never share registers with them
    [loop]
    for (uint k = nearFirst ? 1u : 0u; k < kMaxLitSurfaces; ++k)
    {
        // Picked by constant index since a slot array read at k would move the arrays to local memory
        float slotTk = 0.0;
        uint slotKeyK = 0u;
        float3 slotWeightK = float3(0.0, 0.0, 0.0);
        [unroll]
        for (uint j = 0u; j < kMaxLitSurfaces; ++j)
        {
            if (j == k)
            {
                slotTk = slotT[j];
                slotKeyK = slotKey[j];
                slotWeightK = slotWeight[j];
            }
        }
        const int slotAxisK = int(slotKeyK >> 30u);
        const uint slotMaterialK = slotKeyK & 0x3FFFFFFFu;
        // Weights only fall so nothing past a dim surface shows either
        if (k >= litInterfaces || max(slotWeightK.x, max(slotWeightK.y, slotWeightK.z)) < kContributionCutoff)
        {
            break;
        }
        if (lightCached && slotAxisK == lightAxis && slotMaterialK == lightMaterial)
        {
            result.color += slotWeightK * cachedLight;
            continue;
        }

        float3 voxelNormal = float3(0.0, 0.0, 0.0);
        voxelNormal[slotAxisK] = direction[slotAxisK] > 0.0 ? -1.0 : 1.0;
        const float3 normal = normalize(mul(transpose((float3x3)worldToVoxel), voxelNormal));
        float fresnel;
        bool debugView = false;
        const float3 light = shadeInterface(camera + worldDirection * slotTk, normal, view, input.position.xy,
                                            samplePalette(record, slotMaterialK), false, false, fresnel, debugView);
        if (debugView)
        {
            result.color = light;
            result.transmittance = float3(0.0, 0.0, 0.0);
            result.debugView = true;
            return true;
        }
        result.color += slotWeightK * light;
        ++result.shaded;
        lightCached = true;
        lightAxis = slotAxisK;
        lightMaterial = slotMaterialK;
        cachedLight = light;
    }

    if (nearFirst)
    {
        const int nearAxis = int(nearKey >> 30u);
        float3 voxelNormal = float3(0.0, 0.0, 0.0);
        voxelNormal[nearAxis] = direction[nearAxis] > 0.0 ? -1.0 : 1.0;
        const float3 normal = normalize(mul(transpose((float3x3)worldToVoxel), voxelNormal));
        // What it reflects is too dim to show past the glass in front of it
        const bool reflects = max(nearWeight.x, max(nearWeight.y, nearWeight.z)) * firstFresnel >= kReflectionCutoff;
        float fresnel;
        bool debugView = false;
        result.color += nearWeight * shadeInterface(camera + worldDirection * nearT, normal, view, input.position.xy,
                                                    samplePalette(record, nearKey & 0x3FFFFFFFu), reflects, true, fresnel, debugView);
        ++result.shaded;
        result.reflected = result.reflected || (reflects && kSpecReflection != kReflectionNone);
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

bool blendGlass(VSOutput input, out DualSourceOutput output, out float depth)
{
    output = (DualSourceOutput)0;
    GlassComposite glass;
    if (!compositeGlass<0>(input, glass))
    {
        depth = input.position.z;
        return false;
    }
    depth = glass.depth;
    output.color = float4(glass.color, 1.0);
    output.transmittance = float4(glass.transmittance, 1.0);
    if (gGlassPush.costView != 0u)
    {
        // Added on top of what is behind: green per brick fragment, red per lit surface, blue per reflection
        output.color = float4(0.15 * float(glass.shaded), 0.04, glass.reflected ? 0.3 : 0.0, 1.0);
        output.transmittance = float4(1.0, 1.0, 1.0, 1.0);
    }
    return true;
}

[shader("fragment")]
DualSourceOutput fragmentMain(VSOutput input, out float depth : SV_Depth)
{
    DualSourceOutput output;
    if (!blendGlass(input, output, depth))
    {
        discard;
    }
    return output;
}

[shader("fragment")]
DualSourceOutput fragmentMainOutside(VSOutput input, out float depth : GLASS_FARTHER_DEPTH)
{
    DualSourceOutput output;
    if (!blendGlass(input, output, depth))
    {
        discard;
    }
    // Slab entry can land a hair in front of the face
    depth = fartherDepth(depth, input.position.z);
    return output;
}

/// Far outside bricks at a coarse shading rate, which drivers only honour without a depth export
/// The rasterized front face is closer than the glass and compositeGlass rejects glass behind the opaque depth itself
[shader("fragment")]
DualSourceOutput fragmentMainFar(VSOutput input)
{
    DualSourceOutput output;
    float depth;
    if (!blendGlass(input, output, depth))
    {
        discard;
    }
    return output;
}

/// Without dualSrcBlend the tint is lost and the background only fades
float4 premultipliedGlass(DualSourceOutput blend)
{
    return float4(blend.color.rgb, 1.0 - dot(blend.transmittance.rgb, float3(1.0, 1.0, 1.0)) / 3.0);
}

[shader("fragment")]
float4 fragmentPremultiplied(VSOutput input, out float depth : SV_Depth) : SV_Target0
{
    DualSourceOutput output;
    if (!blendGlass(input, output, depth))
    {
        discard;
    }
    return premultipliedGlass(output);
}

[shader("fragment")]
float4 fragmentPremultipliedFar(VSOutput input) : SV_Target0
{
    DualSourceOutput output;
    float depth;
    if (!blendGlass(input, output, depth))
    {
        discard;
    }
    return premultipliedGlass(output);
}

[shader("fragment")]
float4 fragmentPremultipliedOutside(VSOutput input, out float depth : GLASS_FARTHER_DEPTH) : SV_Target0
{
    DualSourceOutput output;
    if (!blendGlass(input, output, depth))
    {
        discard;
    }
    depth = fartherDepth(depth, input.position.z);
    return premultipliedGlass(output);
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

bool surfaceGlass(VSOutput input, out SurfaceOutput output, out float depth, out float frontTransmittance)
{
    output = (SurfaceOutput)0;
    frontTransmittance = 1.0;
    GlassComposite glass;
    if (!compositeGlass<1>(input, glass))
    {
        depth = input.position.z;
        return false;
    }
    depth = glass.depth;
    frontTransmittance = glass.frontTransmittance;

    const VoxelInstance instance = gVoxelInstances[input.instanceIndex];
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
    return true;
}

/// After every blended draw
[shader("fragment")]
SurfaceOutput fragmentSurface(VSOutput input, out float depth : SV_Depth)
{
    SurfaceOutput output;
    float frontTransmittance;
    if (!surfaceGlass(input, output, depth, frontTransmittance))
    {
        discard;
    }
    return output;
}

[shader("fragment")]
SurfaceOutput fragmentSurfaceOutside(VSOutput input, out float depth : GLASS_FARTHER_DEPTH)
{
    SurfaceOutput output;
    float frontTransmittance;
    if (!surfaceGlass(input, output, depth, frontTransmittance))
    {
        discard;
    }
    depth = fartherDepth(depth, input.position.z);
    return output;
}

/// Pre-pass of the nearest glass surface into its own depth, the bloom glass mask and the surface the copy writes later
struct NearestOutput
{
    float4 mask : SV_Target0;
    float4 normalRoughness : SV_Target1;
    float4 motion : SV_Target2;
};

NearestOutput nearestOutput(SurfaceOutput surface, float frontTransmittance)
{
    NearestOutput output;
    output.mask = float4(1.0, frontTransmittance, 0.0, 1.0);
    output.normalRoughness = surface.normalRoughness;
    output.motion = surface.motion;
    return output;
}

[shader("fragment")]
NearestOutput fragmentNearest(VSOutput input, out float depth : SV_Depth)
{
    SurfaceOutput surface;
    float frontTransmittance;
    if (!surfaceGlass(input, surface, depth, frontTransmittance))
    {
        discard;
    }
    return nearestOutput(surface, frontTransmittance);
}

[shader("fragment")]
NearestOutput fragmentNearestOutside(VSOutput input, out float depth : GLASS_FARTHER_DEPTH)
{
    SurfaceOutput surface;
    float frontTransmittance;
    if (!surfaceGlass(input, surface, depth, frontTransmittance))
    {
        discard;
    }
    depth = fartherDepth(depth, input.position.z);
    return nearestOutput(surface, frontTransmittance);
}

[shader("vertex")]
FullscreenVSOutput vertexFullscreen(uint vertexId : SV_VertexID)
{
    return fullscreenTriangle(vertexId);
}

/// After every blended draw, writes what the pre-pass found where it found glass in front of the opaque depth
[shader("fragment")]
SurfaceOutput fragmentSurfaceCopy(FullscreenVSOutput input, out float depth : SV_Depth)
{
    const int3 texel = int3(int2(input.position.xy), 0);
    // The mask is cleared everywhere each frame while the nearest depth is only copied where glass is
    if (gGlassMask.Load(texel).r < 0.5)
    {
        discard;
    }
    depth = gNearestGlassDepth.Load(texel).r;
    SurfaceOutput output;
    output.color = float4(0.0, 0.0, 0.0, 0.0);
    output.normalRoughness = gNearestGlassNormal.Load(texel);
    output.indirectDiffuse = float4(0.0, 0.0, 0.0, 1.0);
    output.motion = gNearestGlassMotion.Load(texel);
    return output;
}
