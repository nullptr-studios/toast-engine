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
};

[[vk::push_constant]]
GlassPush gGlassPush;

/// Lit glass surfaces per brick and pixel past which layers only absorb
static const uint kMaxLitInterfaces = 4u;

/// Below this nothing behind shows
static const float kOpaqueTransmittance = 0.004;

/// Schlick F0 of window glass
static const float kGlassF0 = 0.04;

struct VSOutput
{
    float4 position : SV_Position;

    [[vk::location(0)]]
    float3 worldPosition : TEXCOORD0;

    [[vk::location(1)]]
    nointerpolation uint instanceIndex : TEXCOORD1;

    [[vk::location(2)]]
    nointerpolation int3 brick : TEXCOORD2;
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
    surface.metallic = palette.metallic;
    surface.reflectivity = palette.reflectivity;
    surface.roughness = clamp(palette.roughness, 0.045, 1.0);
    surface.occlusion = 1.0;
    surface.viewSpaceZ = mul(gCamera.view, float4(hitWorld, 1.0)).z;
    surface.pixel = pixel;

    fresnel = palette.reflectivity * (kGlassF0 + (1.0 - kGlassF0) * pow(1.0 - saturate(dot(normal, view)), 5.0));

    const ShadingResult lit = shadeSurfaceLighting(surface);
    debugView = lit.isDebugView;
    if (debugView)
    {
        return lit.color;
    }

    // Black albedo leaves only the specular so a clear pane still reflects at full strength
    float3 specular = float3(0.0, 0.0, 0.0);
    if (palette.reflectivity > 0.0)
    {
        surface.albedo = float3(0.0, 0.0, 0.0);
        specular = shadeSurfaceLighting(surface).color;
    }
    const float3 diffuse = max(lit.color - specular, float3(0.0, 0.0, 0.0));
    return (diffuse + palette.albedo * palette.emissive) * palette.alpha + specular;
}

/// Every glass layer front to back or with @p surfaceOnly the nearest entered surface unlit
/// Surfaces scatter by alpha and the path filters by lerp(1 albedo alpha) per voxel
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
    if (isVoxelDebugMode(gCamera.renderModePad.x))
    {
        return false;
    }

    const VoxelInstance instance = gVoxelInstances[input.instanceIndex];
    const uint volume = instance.recordIndex;
    const float3 camera = gCamera.position;

    // t = 1 is the back face
    const float3 worldDirection = input.worldPosition - camera;
    const float3 origin = mul(instance.worldToVoxel, float4(camera, 1.0)).xyz;
    const float3 direction = mul(instance.worldToVoxel, float4(worldDirection, 0.0)).xyz;
    const float3 view = -normalize(worldDirection);

    const float nearOrigin = clipNearDistance(mul(gCamera.viewProjection, float4(camera, 1.0)));
    const float nearStep = clipNearDistance(mul(gCamera.viewProjection, float4(worldDirection, 0.0)));
    const float tMin = nearStep > 0.0 ? max(0.0, -nearOrigin / nearStep) : 0.0;

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
    bool inside = glassEntry(volume, voxelMaterialAt(volume, previous));

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
    for (uint step = 0u; step < 3u * kBrickDim; ++step)
    {
        const uint material = voxelMaterialAt(volume, voxel);
        const int nextAxis = next.x < next.y ? (next.x < next.z ? 0 : 2) : (next.y < next.z ? 1 : 2);
        const float tNext = min(next[nextAxis], tExit);

        if (material != 0u)
        {
            // The opaque surface behind is already in the scene colour the transmittance filters
            if (!glassEntry(volume, material))
            {
                break;
            }

            const PaletteSample palette = samplePalette(volume, material);
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
                const float3 normal = normalize(mul(transpose((float3x3)instance.worldToVoxel), voxelNormal));

                if (!result.entered)
                {
                    result.entered = true;
                    result.normal = normal;
                    // SSR only reads roughness so zero reflectivity must look fully rough
                    result.roughness = lerp(1.0, clamp(palette.roughness, 0.045, 1.0), palette.reflectivity);
                    result.f0 = specularF0(palette.albedo, palette.metallic);
                    result.voxelPosition = origin + direction * t;
                    if (surfaceOnly)
                    {
                        firstT = t;
                        break;
                    }
                }

                float fresnel = 0.0;
                if (litInterfaces < kMaxLitInterfaces)
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
                result.transmittance *= (1.0 - fresnel) * (1.0 - palette.alpha) * palette.albedo;
            }
            else if (surfaceOnly)
            {
                // Glass continuing from a nearer brick whose own surface pass writes the surface
                break;
            }

            result.transmittance *= pow(lerp(float3(1.0, 1.0, 1.0), palette.albedo, palette.alpha), thickness);
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
    }

    if (!touched || (surfaceOnly && !result.entered))
    {
        return false;
    }
    const float4 clip = mul(gCamera.viewProjection, float4(camera + worldDirection * firstT, 1.0));
    result.depth = clip.z / clip.w;
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
    DualSourceOutput output;
    output.color = float4(glass.color, 1.0);
    output.transmittance = float4(glass.transmittance, 1.0);
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
