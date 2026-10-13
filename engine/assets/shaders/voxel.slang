import voxel_dda;
import voxel_gbuffer;
import lighting;
import depth_convention;

/// Mirrors renderer::VoxelPass::PushConstants
struct PushConstants
{
    uint instanceIndex;
    uint bucket;
    uint mode;
    uint quadCapacity;
};

[[vk::push_constant]]
PushConstants gPush;

static const uint kDrawVolume = 0u;
static const uint kDrawCells = 1u;

[[vk::binding(7, 1)]]
StructuredBuffer<uint2> gVoxelCells;
[[vk::binding(8, 1)]]
StructuredBuffer<uint> gVoxelCellArgs;

struct VSOutput
{
    float4 position : SV_Position;

    [[vk::location(0)]]
    float3 worldPosition : TEXCOORD0;

    [[vk::location(1)]]
    nointerpolation uint instanceIndex : TEXCOORD1;

    [[vk::location(2)]]
    nointerpolation int3 boxMin : TEXCOORD2;

    [[vk::location(3)]]
    nointerpolation int3 boxMax : TEXCOORD3;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    uint instanceIndex = gPush.instanceIndex;
    uint packedCell = 0u;
    if (gPush.mode == kDrawCells)
    {
        const uint2 cell = gVoxelCells[gVoxelCellArgs[kCellOffsetsWord + gPush.bucket] + instanceId];
        instanceIndex = cell.x;
        packedCell = cell.y;
    }

    const VoxelInstance instance = gVoxelInstances[instanceIndex];
    const VolumeRecord record = gVolumeRecords[instance.recordIndex];
    const int3 dims = int3(record.brickDimsX, record.brickDimsY, record.brickDimsZ) * int(kBrickDim);

    int3 boxMin = int3(0, 0, 0);
    int3 boxMax = dims;
    if (gPush.mode == kDrawCells)
    {
        boxMin = int3(unpackCell(packedCell)) * kCoarseVoxels;
        boxMax = min(boxMin + kCoarseVoxels, dims);
    }

    // A mirrored transform flips winding so swap two corners of each triangle to keep one cull mode
    uint corner = vertexId % 3u;
    if ((instance.flags & kInstanceMirrored) != 0u && corner != 0u)
    {
        corner = 3u - corner;
    }
    const float3 unit = kBoxCorners[kBoxIndices[vertexId - (vertexId % 3u) + corner]];
    const float3 local = float3(boxMin) + unit * float3(boxMax - boxMin);
    const float4 world = mul(instance.voxelToWorld, float4(local, 1.0));

    VSOutput output;
    output.worldPosition = world.xyz;
    output.position = mul(gCamera.jitteredViewProjection, world);
    output.instanceIndex = instanceIndex;
    output.boxMin = boxMin;
    output.boxMax = boxMax;
    return output;
}

// Mirrors the voxel G-buffer attachment order
struct FSOutput
{
    float4 albedo : SV_Target0;

    /// xyz world normal w roughness as SSR
    float4 normalRoughness : SV_Target1;

    float4 material : SV_Target2;

    /// xy uv motion z reactive w 1 when written
    float4 motion : SV_Target3;
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

// VoxelEditor grid modes
static const uint kOverlayUnitGrid = 1u;
static const uint kOverlayVoxelGrid = 2u;
static const uint kOverlayVoxelEdges = 4u;
static const float3 kSelectionTint = float3(1.0, 0.366, 0.0);

/// Voxel contour thing
float voxelEdgeCoverage(uint recordIndex, RayHit hit, float3 cellPosition, float3 voxelsPerPixel, int3 outward)
{
    const uint u = (uint(hit.axis) + 1u) % 3u;
    const uint v = (uint(hit.axis) + 2u) % 3u;
    float coverage = 0.0;
    for (uint side = 0u; side < 4u; ++side)
    {
        const uint axis = side < 2u ? u : v;
        const int sign = (side % 2u) == 0u ? 1 : -1;
        int3 step = int3(0, 0, 0);
        step[axis] = sign;
        const int3 neighbour = hit.voxel + step;
        const bool edge = voxelMaterialAt(recordIndex, neighbour) == 0u || voxelMaterialAt(recordIndex, neighbour + outward) != 0u;
        if (!edge)
        {
            continue;
        }
        const float inside = frac(cellPosition[axis]);
        const float distance = (sign > 0 ? 1.0 - inside : inside) / max(voxelsPerPixel[axis], 1e-6);
        coverage = max(coverage, saturate(1.5 - distance));
    }
    return coverage;
}

/// VoxelEditor grids
float3 applyEditorOverlays(float3 color, VoxelInstance instance, RayHit hit, float3 direction, float3 cellPosition, float3 voxelsPerPixel,
                           float3 hitWorld, float3 worldPerPixel, float3 normal)
{
    const uint overlays = gCamera.renderModePad.y;
    if (hit.axis >= 0)
    {
        const uint u = (uint(hit.axis) + 1u) % 3u;
        const uint v = (uint(hit.axis) + 2u) % 3u;
        const float2 face = float2(cellPosition[u], cellPosition[v]);
        const float2 width = float2(voxelsPerPixel[u], voxelsPerPixel[v]);

        if ((overlays & kOverlayVoxelGrid) != 0u)
        {
            // remove once a voxel is under about three pixels
            const float fade = saturate((0.35 - max(width.x, width.y)) / 0.2);
            color *= 1.0 - 0.35 * fade * gridLine(face, width, 1.0, 0.0);
        }
        if ((overlays & kOverlayUnitGrid) != 0u)
        {
            // Big grud
            const float3 absolute = abs(normal);
            const uint dominant = absolute.x > absolute.y ? (absolute.x > absolute.z ? 0u : 2u) : (absolute.y > absolute.z ? 1u : 2u);
            const uint wu = (dominant + 1u) % 3u;
            const uint wv = (dominant + 2u) % 3u;
            const float2 worldFace = float2(hitWorld[wu], hitWorld[wv]);
            const float2 worldWidth = float2(worldPerPixel[wu], worldPerPixel[wv]);
            color *= 1.0 - 0.6 * gridLine(worldFace, worldWidth, 1.0, 0.5);
        }
        if ((overlays & kOverlayVoxelEdges) != 0u)
        {
            int3 outward = int3(0, 0, 0);
            outward[hit.axis] = direction[hit.axis] > 0.0 ? -1 : 1;
            const float edge = voxelEdgeCoverage(instance.recordIndex, hit, cellPosition, voxelsPerPixel, outward);
            color = lerp(color, float3(0.01, 0.01, 0.01), 0.85 * edge);
        }
    }

    if (instance.highlightRecord != 0u && voxelMaterialAt(instance.highlightRecord - 1u, hit.voxel) != 0u)
    {
        color = lerp(color, kSelectionTint, 0.55);
    }
    return color;
}

/// False discards
bool shadeDebug(uint mode, float4 pixel, VoxelInstance instance, float3 origin, float3 direction, RayHit hit, float3 normal, float3 toCamera, inout FSOutput output)
{
    // Before any branch on the hit since derivatives need uniform control flow
    const float3 cellPosition = origin + direction * hit.t;
    const float3 voxelsPerPixel = fwidth(cellPosition);

    // Zero normal so SSAO and SSR pass the colour through
    output.normalRoughness = float4(0.0, 0.0, 0.0, 0.0);

    const bool showsMisses = mode == kRenderModeVoxelSteps || mode == kRenderModeVoxelTraversal || mode == kRenderModeVoxelVolumes;
    if (!hit.hit && !showsMisses)
    {
        return false;
    }

    const bool stripe = ((uint(pixel.x) + uint(pixel.y)) / 4u) % 2u == 0u;
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
        const int3 brick = hit.voxel / int(kBrickDim);
        const uint tag = gridEntry(instance.recordIndex, uint3(brick)) & 3u;
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
    else if (mode == kRenderModeVoxelPaletteId)
    {
        // pick colors that are different so you can tell them appart easily
        // https://en.wikipedia.org/wiki/Five_color_theorem tye shit
        color = debugHue(frac(float(hit.material) * 0.618034 + 0.12)) * 0.85 + 0.15;
    }
    else
    {
        const PaletteSample palette = samplePalette(instance.recordIndex, hit.material);
        color = palette.material == 0u ? kDefaultMaterialColor * (stripe ? 1.0 : 0.3)
                                       : debugHue(frac(float(palette.material) * 0.618034 + 0.33));
    }

    output.albedo = float4(color * light, kGbufferUnlit);
    return true;
}

/// Shared by the march and the quads so both write the same G-buffer
bool shadeHit(float4 pixel, VoxelInstance instance, float3 origin, float3 direction, float3 worldDirection, RayHit hit,
              out FSOutput output, inout float depth)
{
    output.albedo = float4(0.0, 0.0, 0.0, 0.0);
    output.normalRoughness = float4(0.0, 0.0, 1.0, 1.0);
    output.material = float4(0.0, 0.0, 0.0, 0.0);
    output.motion = float4(0.0, 0.0, 0.0, 0.0);

    const float3 camera = gCamera.position;
    const float3 hitWorld = camera + worldDirection * hit.t;

    const float3 cellPosition = origin + direction * hit.t;
    const float3 voxelsPerPixel = fwidth(cellPosition);
    const float3 worldPerPixel = fwidth(hitWorld);

    // Not camera minus hit since a ray starting inside a solid voxel hits at t = 0
    const float3 toCamera = -normalize(worldDirection);

    float3 normal = toCamera;
    if (hit.hit)
    {
        const float4 clip = mul(gCamera.viewProjection, float4(hitWorld, 1.0));
        depth = clip.z / clip.w;

        // Voxel-space hit through the previous model is where it was last frame
        // Bit 0 set means no real previous frame yet so motion stays the default not-written value
        if ((instance.flags & 1u) == 0u)
        {
            const float4 previousWorld = mul(instance.previousVoxelToWorld, float4(origin + direction * hit.t, 1.0));
            const float4 previousClip = mul(gCamera.previousViewProjection, previousWorld);
            output.motion = float4((clip.xy / clip.w - previousClip.xy / previousClip.w) * 0.5, 0.0, 1.0);
        }

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
        // A miss drawn by a debug view sits on the far plane so a box behind it still wins the depth test
        if (!hit.hit)
        {
            depth = kFarDepth;
        }
        return shadeDebug(renderMode, pixel, instance, origin, direction, hit, normal, toCamera, output);
    }

    if (!hit.hit)
    {
        return false;
    }

    const PaletteSample palette = samplePalette(instance.recordIndex, hit.material);
    const float roughness = clamp(palette.roughness, 0.045, 1.0);

    output.albedo = float4(applyEditorOverlays(palette.albedo, instance, hit, direction, cellPosition, voxelsPerPixel, hitWorld, worldPerPixel, normal), kGbufferLit);

    // SSR only reads roughness so zero reflectivity must look fully rough
    output.normalRoughness = float4(normal, lerp(1.0, roughness, palette.reflectivity));
    output.material = float4(palette.metallic, palette.reflectivity, roughness, encodeEmissive(palette.emissive));
    return true;
}

bool shade(VSOutput input, out FSOutput output, out float depth)
{
    depth = input.position.z;

    const VoxelInstance instance = gVoxelInstances[input.instanceIndex];
    const float3 camera = gCamera.position;

    // t = 1 is the box surface
    const float3 worldDirection = input.worldPosition - camera;
    const float3 origin = mul(instance.worldToVoxel, float4(camera, 1.0)).xyz;
    const float3 direction = mul(instance.worldToVoxel, float4(worldDirection, 0.0)).xyz;

    // Never hit in front of the near plane whose clip distance is linear along the ray in either convention
    const float nearOrigin = clipNearDistance(mul(gCamera.viewProjection, float4(camera, 1.0)));
    const float nearStep = clipNearDistance(mul(gCamera.viewProjection, float4(worldDirection, 0.0)));
    const float tMin = nearStep > 0.0 ? max(0.0, -nearOrigin / nearStep) : 0.0;

    const RayHit hit = marchRayBox(instance.recordIndex, origin, direction, tMin, kFloatMax, input.boxMin, input.boxMax);
    return shadeHit(input.position, instance, origin, direction, worldDirection, hit, output, depth);
}

// The DDA hit is never closer than the rasterized face which keeps early depth testing on
[shader("fragment")]
#if TOAST_REVERSED_Z
FSOutput fragmentOutside(VSOutput input, out float depth : SV_DepthLessEqual)
#else
FSOutput fragmentOutside(VSOutput input, out float depth : SV_DepthGreaterEqual)
#endif
{
    FSOutput output;
    if (!shade(input, output, depth))
    {
        discard;
    }

    // Slab entry can land a hair in front of the face
    depth = fartherDepth(depth, input.position.z);
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

[[vk::binding(9, 1)]]
StructuredBuffer<uint2> gVoxelQuads;

struct QuadVSOutput
{
    float4 position : SV_Position;

    [[vk::location(0)]]
    float3 worldPosition : TEXCOORD0;

    [[vk::location(1)]]
    nointerpolation uint instanceIndex : TEXCOORD1;

    [[vk::location(2)]]
    nointerpolation uint direction : TEXCOORD2;
};

/// Counter clockwise from outside when the plane axes u and v cross to the face normal
static const uint kQuadCorners[6] = { 0, 1, 2, 0, 2, 3 };
static const uint kQuadCornersFlipped[6] = { 0, 2, 1, 0, 3, 2 };

[shader("vertex")]
QuadVSOutput quadVertexMain(uint vertexId : SV_VertexID)
{
    QuadVSOutput output;
    output.position = float4(0.0, 0.0, 0.0, 1.0);
    output.worldPosition = float3(0.0, 0.0, 0.0);
    output.instanceIndex = 0u;
    output.direction = 0u;

    // The mesh pass dropped quads past capacity
    const uint quadIndex = vertexId / 6u;
    if (quadIndex >= gPush.quadCapacity)
    {
        return output;
    }

    const uint2 quad = gVoxelQuads[quadIndex];
    const uint2 cell = gVoxelCells[quad.x];
    const VoxelInstance instance = gVoxelInstances[cell.x];

    const uint direction = quad.y & 7u;
    const uint slice = (quad.y >> 3) & 7u;
    const uint u0 = (quad.y >> 6) & 7u;
    const uint v0 = (quad.y >> 9) & 7u;
    const uint width = ((quad.y >> 12) & 7u) + 1u;
    const uint height = ((quad.y >> 15) & 7u) + 1u;
    const uint brickInCell = (quad.y >> 18) & 63u;

    const uint axis = direction >> 1;
    const bool positive = (direction & 1u) == 0u;
    const uint3 brick = unpackCell(cell.y) * kCoarseBricks + uint3(brickInCell & 3u, (brickInCell >> 2) & 3u, brickInCell >> 4);
    const float3 brickMin = float3(brick * kBrickDim);

    // u cross v is +x for X faces and -y for Y faces and +z for Z faces
    bool flipped = positive == (axis == 1u);
    if ((instance.flags & kInstanceMirrored) != 0u)
    {
        flipped = !flipped;
    }
    const uint corner = flipped ? kQuadCornersFlipped[vertexId % 6u] : kQuadCorners[vertexId % 6u];
    const float cornerU = (corner == 1u || corner == 2u) ? float(width) : 0.0;
    const float cornerV = corner >= 2u ? float(height) : 0.0;

    float3 local = brickMin;
    local[axis] += float(slice) + (positive ? 1.0 : 0.0);
    local[quadAxisU(axis)] += float(u0) + cornerU;
    local[quadAxisV(axis)] += float(v0) + cornerV;
    const float4 world = mul(instance.voxelToWorld, float4(local, 1.0));

    output.position = mul(gCamera.jitteredViewProjection, world);
    output.worldPosition = world.xyz;
    output.instanceIndex = cell.x;
    output.direction = direction;
    return output;
}

[shader("fragment")]
FSOutput quadFragmentMain(QuadVSOutput input)
{
    const VoxelInstance instance = gVoxelInstances[input.instanceIndex];
    const float3 camera = gCamera.position;
    const float3 worldDirection = input.worldPosition - camera;
    const float3 origin = mul(instance.worldToVoxel, float4(camera, 1.0)).xyz;
    const float3 direction = mul(instance.worldToVoxel, float4(worldDirection, 0.0)).xyz;

    const uint axis = input.direction >> 1;
    float3 inward = float3(0.0, 0.0, 0.0);
    inward[axis] = (input.direction & 1u) == 0u ? -0.5 : 0.5;

    // Greedy quads span materials so the voxel behind the pixel picks its own
    RayHit hit;
    hit.t = 1.0;
    hit.voxel = int3(floor(origin + direction + inward));
    hit.axis = int(axis);
    hit.material = voxelMaterialAt(instance.recordIndex, hit.voxel);
    hit.hit = hit.material != 0u;
    hit.steps = 0u;
    hit.coarseSteps = 0u;
    hit.brickSteps = 0u;

    FSOutput output;
    float depth = input.position.z;
    if (!shadeHit(input.position, instance, origin, direction, worldDirection, hit, output, depth))
    {
        discard;
    }
    return output;
}
