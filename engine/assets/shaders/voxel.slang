import voxel_dda;
import lighting;

struct PushConstants
{
    uint instanceIndex;
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::push_constant]]
PushConstants gPush;

struct VSOutput
{
    float4 position : SV_Position;

    [[vk::location(0)]]
    float3 worldPosition : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    const VoxelInstance instance = gVoxelInstances[gPush.instanceIndex];
    const VolumeRecord record = gVolumeRecords[instance.recordIndex];
    const float3 voxels = float3(record.brickDimsX, record.brickDimsY, record.brickDimsZ) * 8.0;

    const float3 local = kBoxCorners[kBoxIndices[vertexId]] * voxels;
    const float4 world = mul(instance.voxelToWorld, float4(local, 1.0));

    VSOutput output;
    output.worldPosition = world.xyz;
    output.position = mul(gCamera.viewProjection, world);
    return output;
}

struct FSOutput
{
    float4 color : SV_Target0;

    /// xyz world normal w roughness
    float4 normalRoughness : SV_Target1;

    float4 indirectDiffuse : SV_Target2;
};

// Mirrored by renderer::voxel_debug for the overlay legend
static const float3 kStepBandColors[8] = {
    float3(0.10, 0.15, 0.55),
    float3(0.10, 0.45, 0.90),
    float3(0.10, 0.75, 0.75),
    float3(0.25, 0.80, 0.25),
    float3(0.90, 0.85, 0.15),
    float3(0.95, 0.50, 0.10),
    float3(0.90, 0.15, 0.10),
    float3(0.85, 0.20, 0.85),
};
static const float3 kExhaustedColor = float3(1.0, 1.0, 1.0);
static const float3 kUniformBrickColor = float3(0.20, 0.40, 0.95);
static const float3 kSharedBrickColor = float3(0.25, 0.75, 0.30);
static const float3 kOwnedBrickColor = float3(0.95, 0.55, 0.12);
static const float3 kDefaultMaterialColor = float3(0.90, 0.10, 0.90);

/// Bands start at 1 4 8 16 32 64 128 and 256 steps
uint stepBand(uint steps)
{
    return min(max(firstbithigh(max(steps, 1u)), 1u) - 1u, 7u);
}

float3 debugHue(float hue)
{
    return saturate(abs(frac(hue + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0);
}

float hash01(int3 value)
{
    uint h = (uint(value.x) * 73856093u) ^ (uint(value.y) * 19349663u) ^ (uint(value.z) * 83492791u);
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return float(h & 0xFFFFu) / 65535.0;
}

/// 1 on a line every period voxels with an antialiased edge half a pixel wide
float gridLine(float2 position, float2 voxelsPerPixel, float period, float halfWidthPixels)
{
    const float2 cell = position / period;
    const float2 pixels = abs(cell - round(cell)) * period / max(voxelsPerPixel, float2(1e-6, 1e-6));
    return saturate(halfWidthPixels + 0.5 - min(pixels.x, pixels.y));
}

/// False discards
bool shadeDebug(uint mode, VSOutput input, VoxelInstance instance, float3 origin, float3 direction, RayHit hit, float3 normal, float3 toCamera, inout FSOutput output)
{
    // Before any branch on the hit since derivatives need uniform control flow
    const float3 cellPosition = origin + direction * hit.t;
    const float3 voxelsPerPixel = fwidth(cellPosition);

    // Zero normal so SSAO and SSR pass the colour through
    output.normalRoughness = float4(0.0, 0.0, 0.0, 0.0);
    output.indirectDiffuse = float4(0.0, 0.0, 0.0, 1.0);

    const bool showsMisses = mode == kRenderModeVoxelSteps || mode == kRenderModeVoxelTraversal || mode == kRenderModeVoxelVolumes;
    if (!hit.hit && !showsMisses)
    {
        return false;
    }

    const bool stripe = ((uint(input.position.x) + uint(input.position.y)) / 4u) % 2u == 0u;
    const float light = hit.hit ? 0.55 + 0.45 * saturate(dot(normal, toCamera)) : (stripe ? 0.5 : 0.2);

    float3 color = float3(0.0, 0.0, 0.0);
    if (mode == kRenderModeVoxelSteps)
    {
        const bool exhausted = !hit.hit && hit.steps >= kMaxMarchSteps;
        color = exhausted ? kExhaustedColor : kStepBandColors[stepBand(hit.steps)];
    }
    else if (mode == kRenderModeVoxelTraversal)
    {
        // Red single voxel steps green brick skips blue coarse skips
        const float3 share = float3(float(hit.steps - hit.coarseSteps - hit.brickSteps), float(hit.brickSteps), float(hit.coarseSteps));
        color = share / max(max(share.x, max(share.y, share.z)), 1.0);
    }
    else if (mode == kRenderModeVoxelVolumes)
    {
        color = debugHue(frac(float(instance.recordIndex) * 0.618034 + 0.05)) * 0.85 + 0.15;
    }
    else if (mode == kRenderModeVoxelBricks)
    {
        const VolumeRecord record = gVolumeRecords[instance.recordIndex];
        const int3 brick = hit.voxel / int(kBrickDim);
        const uint tag = gridEntry(record, uint3(brick)) & 3u;
        color = tag == kTagUniform ? kUniformBrickColor : (tag == kTagShared ? kSharedBrickColor : kOwnedBrickColor);
        color *= 0.7 + 0.3 * hash01(brick);

        if (hit.axis >= 0)
        {
            const uint u = (uint(hit.axis) + 1u) % 3u;
            const uint v = (uint(hit.axis) + 2u) % 3u;
            const float2 face = float2(cellPosition[u], cellPosition[v]);
            const float2 width = float2(voxelsPerPixel[u], voxelsPerPixel[v]);

            // Voxel lines fade out once a voxel is under about three pixels
            const float voxelFade = saturate((0.35 - max(width.x, width.y)) / 0.2);
            color *= 1.0 - 0.3 * voxelFade * gridLine(face, width, 1.0, 0.0);
            color *= 1.0 - 0.8 * gridLine(face, width, float(kBrickDim), 0.5);
            color = lerp(color, float3(1.0, 1.0, 1.0), 0.8 * gridLine(face, width, float(kCoarseVoxels), 0.5));
        }
    }
    else
    {
        const PaletteSample palette = samplePalette(gVolumeRecords[instance.recordIndex], hit.material);
        color = palette.material == 0u ? kDefaultMaterialColor * (stripe ? 1.0 : 0.3)
                                       : debugHue(frac(float(palette.material) * 0.618034 + 0.33));
    }

    output.color = float4(color * light, 1.0);
    return true;
}

bool shade(VSOutput input, out FSOutput output, out float depth)
{
    output.color = float4(0.0, 0.0, 0.0, 1.0);
    output.normalRoughness = float4(0.0, 0.0, 1.0, 1.0);
    output.indirectDiffuse = float4(0.0, 0.0, 0.0, 1.0);
    depth = input.position.z;

    const VoxelInstance instance = gVoxelInstances[gPush.instanceIndex];
    const float3 camera = gCamera.position;

    // t = 1 is the box surface
    const float3 worldDirection = input.worldPosition - camera;
    const float3 origin = mul(instance.worldToVoxel, float4(camera, 1.0)).xyz;
    const float3 direction = mul(instance.worldToVoxel, float4(worldDirection, 0.0)).xyz;

    // Vulkan clips at z = 0 so never hit in front of the near plane
    const float clipOriginZ = mul(gCamera.viewProjection, float4(camera, 1.0)).z;
    const float clipStepZ = mul(gCamera.viewProjection, float4(worldDirection, 0.0)).z;
    const float tMin = clipStepZ > 0.0 ? max(0.0, -clipOriginZ / clipStepZ) : 0.0;

    const RayHit hit = marchRay(instance.recordIndex, origin, direction, tMin, kFloatMax);

    const float3 hitWorld = camera + worldDirection * hit.t;

    // Not camera minus hit since a ray starting inside a solid voxel hits at t = 0
    const float3 toCamera = -normalize(worldDirection);

    float3 normal = toCamera;
    if (hit.hit)
    {
        const float4 clip = mul(gCamera.viewProjection, float4(hitWorld, 1.0));
        depth = clip.z / clip.w;

        if (hit.axis >= 0)
        {
            float3 voxelNormal = float3(0.0, 0.0, 0.0);
            voxelNormal[hit.axis] = direction[hit.axis] > 0.0 ? -1.0 : 1.0;
            normal = normalize(mul(transpose((float3x3)instance.worldToVoxel), voxelNormal));
        }
    }

    const uint renderMode = gCamera.renderModePad.x;
    if (isVoxelDebugMode(renderMode))
    {
        return shadeDebug(renderMode, input, instance, origin, direction, hit, normal, toCamera, output);
    }

    if (!hit.hit)
    {
        return false;
    }

    const VolumeRecord record = gVolumeRecords[instance.recordIndex];
    const PaletteSample palette = samplePalette(record, hit.material);
    const float roughness = clamp(palette.roughness, 0.045, 1.0);

    output.normalRoughness = float4(normal, roughness);

    Surface surface;
    surface.worldPosition = hitWorld;
    surface.normal = normal;
    surface.geometricNormal = normal;
    surface.viewDirection = toCamera;
    surface.albedo = palette.albedo;
    surface.metallic = palette.metallic;
    surface.roughness = roughness;
    surface.occlusion = 1.0;
    surface.viewSpaceZ = mul(gCamera.view, float4(hitWorld, 1.0)).z;
    surface.pixel = input.position.xy;

    const ShadingResult lit = shadeSurfaceLighting(surface);
    output.indirectDiffuse = float4(lit.indirectDiffuse, 1.0);

    if (lit.isDebugView)
    {
        output.color = float4(lit.color, 1.0);
        return true;
    }

    output.color = float4(lit.color + palette.albedo * palette.emissive, 1.0);
    return true;
}

[shader("fragment")]
FSOutput fragmentOutside(VSOutput input, out float depth : SV_DepthGreaterEqual)
{
    FSOutput output;
    if (!shade(input, output, depth))
    {
        discard;
    }

    // Slab entry can land a hair in front of the face
    depth = max(depth, input.position.z);
    return output;
}

[shader("fragment")]
FSOutput fragmentInside(VSOutput input, out float depth : SV_Depth)
{
    FSOutput output;
    if (!shade(input, output, depth))
    {
        discard;
    }
    return output;
}
