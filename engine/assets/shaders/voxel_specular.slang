// Set 0 is the engine scene and set 1 the voxel storage with the scene trace
import fullscreen;
import depth_convention;
import voxel_dda;
import voxel_gbuffer;
import lighting;
import gi_common;
import gi_sample;
import scene_trace;
import trace_shading;
import ggx_sampling;
import screen_march;

// Mirrors TracedSpecularPass::Params
struct SpecularParams
{
    /// Jittered since depth was rasterized with it
    float4x4 inverseViewProjection;
    /// x reach in meters y max roughness z 1 with a valid history w sun reach in meters
    float4 tracing;
    /// x 1 when gPreviousColour holds last frame resolve
    float4 resolve;
    /// x levels of gHiz y most screen march steps with 0 off z SSR thickness w 1 when off screen hits take local lights
    float4 march;
};

[[vk::push_constant]]
ConstantBuffer<SpecularParams> gParams;

[[vk::binding(0, 2)]]
Sampler2D gSceneColour;

[[vk::binding(1, 2)]]
Sampler2D gDepth;

[[vk::binding(2, 2)]]
Sampler2D gAlbedo;

[[vk::binding(3, 2)]]
Sampler2D gMaterial;

[[vk::binding(4, 2)]]
Sampler2D gFaceNormal;

/// Normal with w kTracedMark where voxel lighting left the specular to this pass
[[vk::binding(5, 2)]]
Sampler2D gSceneNormal;

/// rgb radiance a meters to the hit or kMissDistance and kNoTrace for none
[[vk::binding(6, 2)]]
Sampler2D gTrace;

[[vk::binding(7, 2)]]
Sampler2D gHistory;

/// xy uv motion z reactive w 1 when written
[[vk::binding(8, 2)]]
Sampler2D gMotion;

[[vk::binding(9, 2)]]
Sampler2D gTemporal;

/// Last frame TAA resolve since this frame colour is still noisy
[[vk::binding(10, 2)]]
Sampler2D gPreviousColour;

/// rgb radiance a packHistory and kNoTrace for none
[[vk::binding(11, 2)]]
Sampler2D gResolved;

/// packSurface per half resolution texel
[[vk::binding(12, 2)]]
Sampler2D<uint4> gSurface;

/// Last frame resolve box filtered to half resolution then halved per level
[[vk::binding(13, 2)]]
Sampler2D gColourPyramid;

/// Closest depth renderer::HiZPyramid of gDepth for the screen march
[[vk::binding(14, 2)]]
Sampler2D gHiz;

/// gTemporal filtered over the lobe footprint
[[vk::binding(15, 2)]]
Sampler2D gFiltered;

/// x asuint(t) y kHitShade where traceMain left a voxel hit for shadeMain
[[vk::binding(16, 2)]]
Sampler2D<uint4> gHit;

static const uint kHitShade = 1u;
/// Meters either side of the stored hit shadeMain traces again to find its voxel
static const float kHitRefind = 0.25;

/// Mirrors voxel_lighting.slang
static const float kTracedMark = 2.0;
static const float kVoxelSize = 0.1;
/// Ray origin offset off its face in voxels
static const float kOriginOffset = 0.6;

static const float kNoTrace = -1.0;
/// Below this a surface is a mirror and traces the reflection itself
static const float kMirrorRoughness = 0.05;
/// Trims the noisy GGX tail
static const float kLobeTrim = 0.95;
/// Meters off the plane of the hit face the visible surface may sit and still count as the same face
static const float kScreenPlane = 0.025;
/// History roughness further off than this belongs to another material
static const float kRoughnessChange = 0.05;
/// Frames of history a texel counts up to so 1 / count stops at the floor blend
static const float kMaxHistory = 15.0;
/// Trace distance of a ray that left the scene since its radiance came from a direction not a point
static const float kMissDistance = 60000.0;

/// Covers the 4x4 stratum tile
static const int kResolveReach = 2;
/// Lobe over pdf a single ray may reach so one unlikely neighbour cannot take the texel
static const float kMaxRayWeight = 8.0;

/// Mirrors TracedSpecularPass::k_pyramid_levels
static const float kPyramidLevels = 6.0;
/// Share of the GGX lobe volume a footprint spans
static const float kFootprintVolume = 0.75;

/// Tangent of the cone holding kFootprintVolume of the reflected lobe
float lobeTanHalf(float roughness)
{
    return roughness * roughness * kFootprintVolume / (1.0 - kFootprintVolume);
}

/// Frames of history in the integer part and roughness in the fraction
float packHistory(float frames, float roughness)
{
    return min(frames, kMaxHistory) + 0.9 * roughness;
}

float historyFrames(float packed)
{
    return floor(packed);
}

float historyRoughness(float packed)
{
    return frac(packed) / 0.9;
}

typedef FullscreenVSOutput VSOutput;

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    return fullscreenTriangle(vertexId);
}

float3 worldAt(int2 pixel, float depth)
{
    uint width;
    uint height;
    gDepth.GetDimensions(width, height);
    const float2 uv = (float2(pixel) + 0.5) / float2(float(width), float(height));
    const float4 world = mul(gParams.inverseViewProjection, float4(uv * 2.0 - 1.0, depth, 1.0));
    return world.xyz / world.w;
}

struct TracedPixel
{
    bool traced;
    float3 position;
    float3 normal;
    float3 face;
    float3 albedo;
    float metallic;
    float reflectivity;
    float roughness;
};

/// Not traced once glass or a mesh drawn after voxel lighting rewrote the scene normal
TracedPixel tracedPixelAt(int2 pixel)
{
    TracedPixel surface;
    surface.traced = false;
    surface.position = float3(0.0, 0.0, 0.0);
    surface.normal = float3(0.0, 0.0, 1.0);
    surface.face = float3(0.0, 0.0, 1.0);
    surface.albedo = float3(0.0, 0.0, 0.0);
    surface.metallic = 0.0;
    surface.reflectivity = 0.0;
    surface.roughness = 1.0;

    const int3 texel = int3(pixel, 0);
    const float4 encoded = gSceneNormal.Load(texel);
    const float depth = gDepth.Load(texel).r;
    if (encoded.w < 0.5 * (1.0 + kTracedMark) || isFarDepth(depth))
    {
        return surface;
    }
    const float4 albedo = gAlbedo.Load(texel);
    const float4 material = gMaterial.Load(texel);
    surface.traced = true;
    surface.position = worldAt(pixel, depth);
    surface.face = decodeFaceNormal(gFaceNormal.Load(texel));
    surface.normal = dot(encoded.xyz, encoded.xyz) > 1e-4 ? normalize(encoded.xyz) : surface.face;
    surface.albedo = albedo.rgb;
    surface.metallic = material.r;
    surface.reflectivity = saturate(material.g);
    surface.roughness = clamp(material.b, 0.045, 1.0);
    return surface;
}

/// Last frame colour at a pixel blurred to the width the cone of a ray spans at @p distance from the camera
float3 screenColour(int2 pixel, float distance, float coneWidth)
{
    uint width;
    uint height;
    gDepth.GetDimensions(width, height);
    float3 radiance = gSceneColour.Load(int3(pixel, 0)).rgb;
    const float4 motion = gMotion.Load(int3(pixel, 0));
    const float2 previousUv = (float2(pixel) + 0.5) / float2(float(width), float(height)) - (motion.w > 0.5 ? motion.xy : float2(0.0, 0.0));
    if (gParams.resolve.x > 0.5 && motion.z <= 0.5 && all(previousUv >= float2(0.0, 0.0)) && all(previousUv < float2(1.0, 1.0)))
    {
        // Pixels across the cone at the hit and level 0 of the pyramid spans two
        const float pixels = coneWidth * abs(gCamera.projection[1][1]) * float(height) / (2.0 * distance);
        const float level = log2(max(pixels, 1e-4)) - 1.0;
        // By uv since TAA output is output sized
        radiance = level > 0.0 ? gColourPyramid.SampleLevel(previousUv, min(level, kPyramidLevels - 1.0)).rgb
                               : gPreviousColour.SampleLevel(previousUv, 0.0).rgb;
    }
    return radiance;
}

/// False unless the pixel the hit projects to shows the same face. The pyramid level matches the cone of this ray at the hit
bool screenRadiance(float3 position, float3 normal, float coneWidth, out float3 radiance)
{
    radiance = float3(0.0, 0.0, 0.0);
    const float3 toCamera = gCamera.position - position;
    if (dot(normal, toCamera) <= 0.0)
    {
        return false;
    }
    const float4 clip = mul(gCamera.jitteredViewProjection, float4(position, 1.0));
    if (clip.w <= 0.0)
    {
        return false;
    }
    const float2 ndc = clip.xy / clip.w;
    if (any(abs(ndc) >= 1.0))
    {
        return false;
    }
    uint width;
    uint height;
    gDepth.GetDimensions(width, height);
    const int2 pixel = int2((ndc * 0.5 + 0.5) * float2(float(width), float(height)));
    const float depth = gDepth.Load(int3(pixel, 0)).r;
    if (isFarDepth(depth))
    {
        return false;
    }
    const float distance = length(toCamera);
    const float3 offset = worldAt(pixel, depth) - position;
    if (abs(dot(offset, normal)) > kScreenPlane + 0.002 * distance || length(offset) > 0.2 + 0.02 * distance)
    {
        return false;
    }
    radiance = screenColour(pixel, distance, coneWidth);
    return true;
}


/// rayTan is the cone half angle tangent each of the strata rays stands for
float3 hitRadiance(SceneHit hit, float3 origin, float3 direction, float coneSlope, float rayTan, float2 pixel, inout uint random)
{
    const float3 position = origin + direction * hit.t;
    const float3 normal = sceneHitNormal(hit, direction);
    float3 radiance;
    if (screenRadiance(position, normal, 2.0 * rayTan * hit.t, radiance))
    {
        return radiance;
    }

    const PaletteSample palette = samplePalette(gTraceVolumes[hit.volume].recordIndex, hit.material);
    float3 irradiance;
    if (!giHitIrradianceNearest(position, normal, irradiance))
    {
        irradiance = gCamera.ambientColorIntensity.rgb * gCamera.ambientColorIntensity.a;
        if (gCamera.environmentParams.x >= 0.5)
        {
            irradiance += gIrradianceMap.SampleLevel(normal, 0.0).rgb;
        }
    }
    const float3 local =
        gParams.march.w > 0.5 ? traceLocalLight(position, normal, palette.albedo, coneSlope, random) : float3(0.0, 0.0, 0.0);
    return palette.albedo * (palette.emissive + irradiance) + local +
           traceSunLight(position, normal, palette.albedo, coneSlope, gParams.tracing.w, pixel);
}

/// Face plane distance since camera distance jumps every pixel on a grazing floor
float planeWeight(TracedPixel surface, float3 position, float distance)
{
    return exp(-abs(dot(surface.face, position - surface.position)) / (0.005 * distance + 0.01));
}

/// Visible normal GGX sample and its pdf
float3 rayDirection(TracedPixel surface, float2 u, out float pdf)
{
    const float3 N = surface.normal;
    const float3 V = normalize(gCamera.position - surface.position);
    const float3 mirror = reflect(-V, N);
    const float alpha = max(surface.roughness * surface.roughness, 1e-3);
    float3 L = mirror;
    if (surface.roughness > kMirrorRoughness)
    {
        float3 tangent;
        float3 bitangent;
        perpendicularBasis(N, tangent, bitangent);
        const float3 localView = float3(dot(V, tangent), dot(V, bitangent), max(dot(N, V), 1e-4));
        const float3 h = sampleVisibleNormal(localView, alpha, float2(u.x * kLobeTrim, u.y));
        L = reflect(-V, normalize(tangent * h.x + bitangent * h.y + N * h.z));
    }
    // Folded above the face so a grazing lobe stays glossy
    if (dot(L, surface.face) <= 0.0)
    {
        L = normalize(L - 2.0 * dot(L, surface.face) * surface.face);
    }
    if (dot(L, surface.face) <= 0.0)
    {
        L = mirror;
    }
    pdf = visibleNormalPdf(max(dot(N, V), 1e-4), saturate(dot(N, normalize(V + L))), alpha);
    return L;
}

struct LobeFrame
{
    float4x4 worldToVoxel;
    /// Voxel axes spanning the face plane
    int2 axes;
};

/// Probes back along the view ray since the gbuffer has no volume id
LobeFrame lobeFrameOf(TracedPixel surface)
{
    LobeFrame frame;
    const float scale = 1.0 / kVoxelSize;
    frame.worldToVoxel = float4x4(scale, 0.0, 0.0, 0.0, 0.0, scale, 0.0, 0.0, 0.0, 0.0, scale, 0.0, 0.0, 0.0, 0.0, 1.0);
    const float3 toCamera = gCamera.position - surface.position;
    const float distance = length(toCamera);
    const float3 V = toCamera / max(distance, 1e-4);
    const float reach = 0.05 + 0.01 * distance;
    const SceneHit hit = traceScene(surface.position + V * reach, -V, 0.0, 2.0 * reach, 0.0);
    if (hit.hit)
    {
        frame.worldToVoxel = gTraceVolumes[hit.volume].worldToVoxel;
    }
    int axis = hit.hit ? hit.axis : -1;
    if (axis < 0)
    {
        const float3 normal = abs(mul((float3x3)frame.worldToVoxel, surface.face));
        axis = normal.x >= normal.y && normal.x >= normal.z ? 0 : (normal.y >= normal.z ? 1 : 2);
    }
    frame.axes = axis == 0 ? int2(1, 2) : (axis == 1 ? int2(0, 2) : int2(0, 1));
    return frame;
}

/// On the unjittered ray so TAA jitter never moves the pattern
float2 faceCoordinate(TracedPixel surface, LobeFrame frame, float2 ndc, float2 jitter, float depth)
{
    const float4 onRay = mul(gParams.inverseViewProjection, float4(ndc + jitter, depth, 1.0));
    const float3 direction = onRay.xyz / onRay.w - gCamera.position;
    const float facing = dot(direction, surface.face);
    const float t = abs(facing) > 1e-6 ? dot(surface.position - gCamera.position, surface.face) / facing : 1.0;
    const float3 voxel = mul(frame.worldToVoxel, float4(gCamera.position + direction * t, 1.0)).xyz;
    return float2(voxel[frame.axes.x], voxel[frame.axes.y]);
}

uint surfaceStratum(TracedPixel surface, LobeFrame frame, int2 pixel)
{
    if (surface.roughness <= kMirrorRoughness)
    {
        return 0u;
    }
    uint width;
    uint height;
    gDepth.GetDimensions(width, height);
    const float2 size = float2(float(width), float(height));
    const float4 jittered = mul(gCamera.jitteredViewProjection, float4(surface.position, 1.0));
    const float4 unjittered = mul(gCamera.viewProjection, float4(surface.position, 1.0));
    const float2 jitter = jittered.xy / jittered.w - unjittered.xy / unjittered.w;
    const float depth = jittered.z / jittered.w;
    const float2 ndc = (float2(pixel) + 0.5) / size * 2.0 - 1.0;
    const float2 texel = 4.0 / size;

    const float2 centre = faceCoordinate(surface, frame, ndc, jitter, depth);
    const float2 alongX = faceCoordinate(surface, frame, ndc + float2(texel.x, 0.0), jitter, depth) - centre;
    const float2 alongY = faceCoordinate(surface, frame, ndc + float2(0.0, texel.y), jitter, depth) - centre;
    // Power of two so zooming rescales the pattern only at octaves
    const float2 cell = exp2(round(log2(max(max(abs(alongX), abs(alongY)), float2(1e-4, 1e-4)))));
    return lobeStratumIndex(int2(floor(centre / cell)));
}

/// xyz position bits w normal octahedron 11 and 11 bits then roughness 6 and stratum 4
uint4 packSurface(TracedPixel surface, uint stratum)
{
    const uint2 normal = uint2(round(saturate(giOctEncode(surface.normal)) * 2047.0));
    const uint roughness = uint(round(saturate(surface.roughness) * 63.0));
    return uint4(asuint(surface.position), normal.x | (normal.y << 11) | (roughness << 22) | (stratum << 28));
}

/// Neighbours passing the geometry weight share the face of the reader
TracedPixel unpackSurface(uint4 packed, float3 face)
{
    TracedPixel surface;
    surface.traced = true;
    surface.position = asfloat(packed.xyz);
    surface.normal = giOctDecode(float2(packed.w & 2047u, (packed.w >> 11) & 2047u) / 2047.0);
    surface.face = face;
    surface.albedo = float3(0.0, 0.0, 0.0);
    surface.metallic = 0.0;
    surface.reflectivity = 0.0;
    surface.roughness = max(float((packed.w >> 22) & 63u) / 63.0, 0.045);
    return surface;
}

uint packedStratum(uint4 packed)
{
    return packed.w >> 28;
}

[shader("fragment")]
uint4 surfaceMain(VSOutput input) : SV_Target0
{
    const int2 pixel = int2(input.position.xy) * 2;
    const TracedPixel surface = tracedPixelAt(pixel);
    if (!surface.traced)
    {
        return uint4(0u, 0u, 0u, 0u);
    }
    const uint stratum = surface.roughness > kMirrorRoughness ? surfaceStratum(surface, lobeFrameOf(surface), pixel) : 0u;
    return packSurface(surface, stratum);
}

/// The ray of a half resolution texel, shared so shadeMain rebuilds exactly the ray traceMain cast instead of storing it
struct SpecularRay
{
    float3 origin;
    float3 direction;
    /// Rough lobes blur what they hit so their marches take a cone floor
    float coneSlope;
    /// Each stratum ray covers its share of the lobe so its screen hit reads a matching blur
    float rayTan;
};

SpecularRay specularRay(TracedPixel surface, int2 texel)
{
    // Quantized like the resolve regenerates it
    const uint4 packed = gSurface.Load(int3(texel, 0));
    float pdf;
    SpecularRay ray;
    ray.direction = rayDirection(unpackSurface(packed, surface.face), lobeStratumSample(packedStratum(packed)), pdf);
    ray.coneSlope = 0.25 * surface.roughness * surface.roughness;
    ray.origin = surface.position + surface.face * (kOriginOffset * kVoxelSize);
    ray.rayTan = surface.roughness > kMirrorRoughness ? lobeTanHalf(surface.roughness) / sqrt(float(kLobeStrata)) : 0.0;
    return ray;
}

/// Frame in the seed so history averages the light pick instead of freezing it
uint hitRandom(int2 texel)
{
    return giHash((uint(texel.x) | (uint(texel.y) << 16)) ^ giHash(uint(gCamera.noiseParams.z)));
}

struct TraceOutput
{
    float4 trace : SV_Target0;
    /// See gHit, zero unless shadeMain lights this texel
    uint4 hit : SV_Target1;
};

TraceOutput traceResult(float4 trace)
{
    TraceOutput output;
    output.trace = trace;
    output.hit = uint4(0u, 0u, 0u, 0u);
    return output;
}

/// A miss returns the environment term lighting would have added
/// A voxel hit is left to shadeMain so the trace never holds the hit lighting registers
[shader("fragment")]
TraceOutput traceMain(VSOutput input)
{
    const int2 pixel = int2(input.position.xy) * 2;
    const TracedPixel surface = tracedPixelAt(pixel);
    if (!surface.traced)
    {
        return traceResult(float4(0.0, 0.0, 0.0, kNoTrace));
    }

    const int2 texel = int2(input.position.xy);
    const SpecularRay ray = specularRay(surface, texel);
    const float3 L = ray.direction;
    const float3 mirror = reflect(-normalize(gCamera.position - surface.position), surface.normal);
    if (dot(L, surface.face) <= 0.0)
    {
        return traceResult(float4(ambientSpecularRadiance(surface.position, mirror, surface.roughness), kMissDistance));
    }

    const float coneSlope = ray.coneSlope;
    const float3 origin = ray.origin;
    const float rayTan = ray.rayTan;

    // Screen first so a hit the depth buffer shows skips the voxel trace and the hit lighting
    if (gParams.march.y > 0.5)
    {
        ScreenMarch march;
        march.viewProjection = gCamera.jitteredViewProjection;
        march.inverseViewProjection = gParams.inverseViewProjection;
        march.reach = gParams.tracing.x;
        march.thickness = gParams.march.z;
        march.maxSteps = uint(gParams.march.y);
        march.levels = int(gParams.march.x);
        // Fixed per texel so the stable strata stay stable
        const float jitter = float(giHash(uint(texel.x) | (uint(texel.y) << 16)) & 0xffffu) / 65536.0;
        float2 hitUv;
        float fade;
        if (marchScreenHiz(march, gDepth, gHiz, origin, L, jitter, hitUv, fade))
        {
            uint width;
            uint height;
            gDepth.GetDimensions(width, height);
            const int2 hitPixel = clamp(int2(hitUv * float2(float(width), float(height))), int2(0, 0), int2(int(width), int(height)) - 1);
            const float3 hitPosition = worldAt(hitPixel, gDepth.Load(int3(hitPixel, 0)).r);
            // A ray reaching the back of what the screen shows hit something hidden so the voxel trace decides
            if (dot(gSceneNormal.Load(int3(hitPixel, 0)).xyz, L) < 0.0)
            {
                const float t = length(hitPosition - origin);
                return traceResult(float4(screenColour(hitPixel, length(gCamera.position - hitPosition), 2.0 * rayTan * t), t));
            }
        }
    }

    const SceneHit hit = traceScene(origin, L, 0.0, gParams.tracing.x, coneSlope);
    if (!hit.hit)
    {
        return traceResult(float4(ambientSpecularRadiance(surface.position, mirror, surface.roughness), kMissDistance));
    }
    // The distance alone since carrying the volume, material and axis through the walk cost 16 registers
    TraceOutput output = traceResult(float4(0.0, 0.0, 0.0, hit.t));
    output.hit = uint4(asuint(hit.t), kHitShade, 0u, 0u);
    return output;
}

/// Lights the voxel hits traceMain left over the trace it wrote, a pass of its own so neither holds the other's registers
[shader("fragment")]
float4 shadeMain(VSOutput input) : SV_Target0
{
    const int2 texel = int2(input.position.xy);
    const uint2 record = gHit.Load(int3(texel, 0)).xy;
    if ((record.y & kHitShade) == 0u)
    {
        discard;
    }
    const int2 pixel = texel * 2;
    const TracedPixel surface = tracedPixelAt(pixel);
    const SpecularRay ray = specularRay(surface, texel);
    // Everything before the stored distance is empty and the cone floor grows from t 0, so the short walk lands on the same voxel
    const float t = asfloat(record.x);
    const SceneHit hit = traceScene(ray.origin, ray.direction, max(t - kHitRefind, 0.0), t + kHitRefind, ray.coneSlope);
    if (!hit.hit)
    {
        const float3 mirror = reflect(-normalize(gCamera.position - surface.position), surface.normal);
        return float4(ambientSpecularRadiance(surface.position, mirror, surface.roughness), kMissDistance);
    }
    uint random = hitRandom(texel);
    return float4(hitRadiance(hit, ray.origin, ray.direction, ray.coneSlope, ray.rayTan, float2(pixel), random), hit.t);
}

float specularLuma(float3 colour)
{
    return dot(colour, float3(0.2126, 0.7152, 0.0722));
}

/// Tonemapped so one bright hit does not firefly through the variance box
float3 compress(float3 colour)
{
    return colour / (1.0 + specularLuma(colour));
}

float3 expand(float3 colour)
{
    return colour / max(1.0 - specularLuma(colour), 1e-3);
}

/// Neighbour rays weighted by this lobe over their pdf
[shader("fragment")]
float4 resolveMain(VSOutput input) : SV_Target0
{
    const int2 texel = int2(input.position.xy);
    const float4 own = gTrace.Load(int3(texel, 0));
    if (own.a < 0.0)
    {
        return own;
    }
    const TracedPixel surface = tracedPixelAt(texel * 2);
    const float packed = packHistory(0.0, surface.roughness);
    if (!surface.traced)
    {
        return float4(own.rgb, packed);
    }

    uint width;
    uint height;
    gTrace.GetDimensions(width, height);
    const int2 last = int2(int(width), int(height)) - 1;
    const float3 N = surface.normal;
    const float3 V = normalize(gCamera.position - surface.position);
    const float NdotV = max(dot(N, V), 1e-4);
    const float alpha = max(surface.roughness * surface.roughness, 1e-3);
    const float distance = length(gCamera.position - surface.position);

    float3 sum = float3(0.0, 0.0, 0.0);
    float weightSum = 0.0;
    for (int y = -kResolveReach; y <= kResolveReach; ++y)
    {
        for (int x = -kResolveReach; x <= kResolveReach; ++x)
        {
            const int2 offset = int2(x, y);
            const int2 tap = texel + offset;
            if (any(tap < int2(0, 0)) || any(tap > last))
            {
                continue;
            }
            const float4 hit = gTrace.Load(int3(tap, 0));
            if (hit.a < 0.0)
            {
                continue;
            }
            const uint4 packed = gSurface.Load(int3(tap, 0));
            const TracedPixel neighbour = unpackSurface(packed, surface.face);
            // A neighbour on another surface traced a ray this texel could not have
            const float geometry = lobeStratumWeight(offset) * planeWeight(surface, neighbour.position, distance) *
                                   saturate(dot(neighbour.normal, N) * 4.0 - 3.0);
            if (geometry <= 1e-3)
            {
                continue;
            }

            float pdf;
            const float3 sampled = rayDirection(neighbour, lobeStratumSample(packedStratum(packed)), pdf);
            const float3 hitPoint = neighbour.position + neighbour.face * (kOriginOffset * kVoxelSize) + sampled * hit.a;
            const float3 L = hit.a >= kMissDistance ? sampled : normalize(hitPoint - surface.position);
            const float NdotL = dot(N, L);
            if (NdotL <= 0.0 || pdf <= 0.0)
            {
                continue;
            }
            const float NdotH = saturate(dot(N, normalize(V + L)));
            const float ratio = ggxD(NdotH, alpha) * smithG1(NdotV, alpha) * smithG1(NdotL, alpha) / (4.0 * NdotV) / pdf;
            const float weight = geometry * min(ratio, kMaxRayWeight);
            sum += compress(hit.rgb) * weight;
            weightSum += weight;
        }
    }
    if (weightSum <= 1e-6)
    {
        return float4(own.rgb, packed);
    }
    return float4(expand(sum / weightSum), packed);
}

/// Sigmas of the neighbour box history may sit from the mean like voxel_denoise kSpecularClip
static const float kHistoryClip = 1.25;
/// Blend gained per half resolution texel of motion a frame
static const float kMotionFade = 0.05;

bool historyMatches(float4 value, float roughness)
{
    return value.a >= 0.0 && abs(historyRoughness(value.a) - roughness) <= kRoughnessChange;
}

/// Compressed history from the matching texels of the bilinear footprint then the nearest match so thin surfaces keep it
bool fetchHistory(float2 previousUv, float roughness, int2 last, out float3 colour, out float frames)
{
    colour = float3(0.0, 0.0, 0.0);
    frames = 0.0;
    const float2 size = float2(last + 1);
    const float2 position = previousUv * size - 0.5;
    const int2 base = int2(floor(position));
    const float2 f = position - float2(base);
    float3 sum = float3(0.0, 0.0, 0.0);
    float weightSum = 0.0;
    float youngest = kMaxHistory;
    for (int i = 0; i < 4; ++i)
    {
        const int2 offset = int2(i & 1, i >> 1);
        const int2 tap = base + offset;
        if (any(tap < int2(0, 0)) || any(tap > last))
        {
            continue;
        }
        const float4 value = gHistory.Load(int3(tap, 0));
        const float weight = (offset.x == 1 ? f.x : 1.0 - f.x) * (offset.y == 1 ? f.y : 1.0 - f.y);
        if (weight <= 1e-4 || !historyMatches(value, roughness))
        {
            continue;
        }
        sum += compress(value.rgb) * weight;
        weightSum += weight;
        youngest = min(youngest, historyFrames(value.a));
    }
    if (weightSum > 1e-3)
    {
        colour = sum / weightSum;
        frames = youngest;
        return true;
    }

    const float2 target = previousUv * size;
    const int2 nearest = int2(floor(target));
    float best = 1e9;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const int2 tap = nearest + int2(x, y);
            if (any(tap < int2(0, 0)) || any(tap > last))
            {
                continue;
            }
            const float4 value = gHistory.Load(int3(tap, 0));
            const float2 offset = float2(tap) + 0.5 - target;
            const float distanceSquared = dot(offset, offset);
            if (distanceSquared < best && historyMatches(value, roughness))
            {
                best = distanceSquared;
                colour = compress(value.rgb);
                frames = historyFrames(value.a);
            }
        }
    }
    return best < 1e9;
}

/// Mirrors keep a faster floor blend since surface motion smears them
[shader("fragment")]
float4 temporalMain(VSOutput input) : SV_Target0
{
    const int2 texel = int2(input.position.xy);
    const float4 current = gResolved.Load(int3(texel, 0));
    if (current.a < 0.0 || gParams.tracing.z < 0.5)
    {
        return current;
    }
    const float roughness = historyRoughness(current.a);

    uint width;
    uint height;
    gResolved.GetDimensions(width, height);
    const int2 last = int2(int(width), int(height)) - 1;
    // Same surface only so a thin strip does not borrow the spread of what surrounds it
    float3 mean = float3(0.0, 0.0, 0.0);
    float3 meanSquared = float3(0.0, 0.0, 0.0);
    float count = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const float4 value = gResolved.Load(int3(clamp(texel + int2(x, y), int2(0, 0), last), 0));
            if (!historyMatches(value, roughness))
            {
                continue;
            }
            const float3 mapped = compress(value.rgb);
            mean += mapped;
            meanSquared += mapped * mapped;
            count += 1.0;
        }
    }
    mean /= max(count, 1.0);
    const float3 sigma = sqrt(max(meanSquared / max(count, 1.0) - mean * mean, float3(0.0, 0.0, 0.0)));

    const float4 motion = gMotion.Load(int3(texel * 2, 0));
    const float2 size = float2(float(width), float(height));
    const float2 uv = (float2(texel) + 0.5) / size;
    const float2 movement = motion.w > 0.5 ? motion.xy : float2(0.0, 0.0);
    const float2 previousUv = uv - movement;
    if (motion.z > 0.5 || any(previousUv < float2(0.0, 0.0)) || any(previousUv > float2(1.0, 1.0)))
    {
        return current;
    }
    float3 history;
    float historyAge;
    if (!fetchHistory(previousUv, roughness, last, history, historyAge))
    {
        return current;
    }

    // A young history trusts the box less since it is still mostly noise
    const float frames = historyAge + 1.0;
    const float3 clipped = clamp(history, mean - kHistoryClip * sigma, mean + kHistoryClip * sigma);
    const float floorBlend = lerp(0.2, 0.04, saturate(roughness / max(gParams.tracing.y, 1e-3)));
    // Reflections slide against surface motion so fast movement trusts history less
    const float moving = saturate(length(movement * size) * kMotionFade);
    const float blend = max(max(1.0 / (frames + 1.0), floorBlend), moving);
    return float4(expand(lerp(clipped, compress(current.rgb), blend)), packHistory(frames, roughness));
}

/// 2x2 box of the last frame resolve over each half resolution texel
[shader("fragment")]
float4 pyramidMain(VSOutput input) : SV_Target0
{
    uint width;
    uint height;
    gSurface.GetDimensions(width, height);
    const float2 quarter = 0.25 / float2(float(width), float(height));
    float3 sum = float3(0.0, 0.0, 0.0);
    for (int i = 0; i < 4; ++i)
    {
        const float2 offset = float2(i & 1, i >> 1) * 2.0 - 1.0;
        sum += gPreviousColour.SampleLevel(input.uv + offset * quarter, 0.0).rgb;
    }
    return float4(sum * 0.25, 1.0);
}

/// Gaussian of the 2x2 bilinear core in half resolution texels
static const float kBilinearSigma = 0.6;
/// Vogel disk taps spread over the reflection footprint
static const int kFootprintTaps = 12;
/// Full resolution pixels the footprint radius may reach so 12 taps still overlap
static const float kMaxFootprintPixels = 32.0;
/// Below this the bilinear core already covers the blur
static const float kMinFootprintPixels = 1.5;
/// Elongation cap along the view so grazing floors keep their taps on screen
static const float kMaxStretch = 4.0;
/// Hit distance a miss counts as since its radiance came from far away
static const float kFarHitDistance = 1000.0;

/// Plane distance and normal agreement of the half resolution texel with this surface
float sourceWeight(TracedPixel surface, int2 texel, float distance)
{
    const int2 source = texel * 2;
    const float3 sourceNormal = gSceneNormal.Load(int3(source, 0)).xyz;
    const float facing = dot(sourceNormal, sourceNormal) > 1e-4 ? saturate(dot(normalize(sourceNormal), surface.normal)) : 0.0;
    return planeWeight(surface, worldAt(source, gDepth.Load(int3(source, 0)).r), distance) * pow(facing, 16.0);
}

/// Mean of hit over hit plus camera distance across the raw rays on this surface so contact reflections stay sharp
float footprintHitFactor(TracedPixel surface, int2 base, int2 last, float distance)
{
    float sum = 0.0;
    float weightSum = 0.0;
    for (int y = -1; y <= 2; ++y)
    {
        for (int x = -1; x <= 2; ++x)
        {
            const int2 tap = base + int2(x, y);
            if (any(tap < int2(0, 0)) || any(tap > last))
            {
                continue;
            }
            const float hit = gTrace.Load(int3(tap, 0)).a;
            if (hit < 0.0)
            {
                continue;
            }
            const float weight = planeWeight(surface, asfloat(gSurface.Load(int3(tap, 0)).xyz), distance);
            const float clamped = min(hit, kFarHitDistance);
            sum += clamped / (clamped + distance) * weight;
            weightSum += weight;
        }
    }
    return weightSum > 1e-4 ? sum / weightSum : 1.0;
}

float2 fullPixelOf(float3 position, float2 fullSize, out bool visible)
{
    const float4 clip = mul(gCamera.jitteredViewProjection, float4(position, 1.0));
    visible = clip.w > 0.0;
    return (clip.xy / max(clip.w, 1e-6) * 0.5 + 0.5) * fullSize;
}

/// Plane distance and normal agreement of a half resolution texel from its packed surface
float packedWeight(TracedPixel surface, int2 texel, float distance)
{
    const uint4 packed = gSurface.Load(int3(texel, 0));
    const float3 normal = giOctDecode(float2(packed.w & 2047u, (packed.w >> 11) & 2047u) / 2047.0);
    return planeWeight(surface, asfloat(packed.xyz), distance) * pow(saturate(dot(normal, surface.normal)), 16.0);
}

/// Disk taps over the GGX lobe footprint at the hit distance projected on the surface plane, at half resolution so the
/// composite only upsamples
[shader("fragment")]
float4 filterMain(VSOutput input) : SV_Target0
{
    const int2 texel = int2(input.position.xy);
    const float4 own = gTemporal.Load(int3(texel, 0));
    if (own.a < 0.0)
    {
        return own;
    }
    const TracedPixel surface = tracedPixelAt(texel * 2);
    if (!surface.traced)
    {
        return own;
    }

    uint width;
    uint height;
    gTemporal.GetDimensions(width, height);
    const int2 last = int2(int(width), int(height)) - 1;
    const float distance = length(gCamera.position - surface.position);
    float3 reflection = compress(own.rgb);
    float weightSum = 1.0;

    // Lobe radius on the plane is its tangent times the distance to the virtual image seen from the camera
    const float3 V = normalize(gCamera.position - surface.position);
    float radius = lobeTanHalf(surface.roughness) * distance * footprintHitFactor(surface, texel, last, distance);
    const float3 N = surface.face;
    float3 along = V - N * dot(V, N);
    float3 across;
    if (dot(along, along) > 1e-6)
    {
        along = normalize(along);
        across = cross(N, along);
    }
    else
    {
        perpendicularBasis(N, along, across);
    }
    // Reflections stretch toward the viewer as the view grazes the surface
    const float stretch = min(1.0 / max(abs(dot(V, N)), 1e-3), kMaxStretch);

    const float2 fullSize = float2(float(width), float(height)) * 2.0;
    bool visible;
    const float2 centre = fullPixelOf(surface.position, fullSize, visible);
    const float2 alongPixels = fullPixelOf(surface.position + along * (radius * stretch), fullSize, visible) - centre;
    const float2 acrossPixels = fullPixelOf(surface.position + across * radius, fullSize, visible) - centre;
    const float pixels = max(length(alongPixels), length(acrossPixels));
    if (pixels > kMinFootprintPixels)
    {
        radius *= min(1.0, kMaxFootprintPixels / pixels);
        const float turn = interleavedGradientNoise(float2(texel)) * 6.28318530718;
        for (int i = 0; i < kFootprintTaps; ++i)
        {
            const float r = sqrt((float(i) + 0.5) / float(kFootprintTaps));
            const float angle = float(i) * 2.39996323 + turn;
            const float3 position = surface.position + (along * (cos(angle) * stretch) + across * sin(angle)) * (r * radius);
            const int2 tap = int2(floor(fullPixelOf(position, fullSize, visible) * 0.5));
            if (!visible || any(tap < int2(0, 0)) || any(tap > last))
            {
                continue;
            }
            const float4 value = gTemporal.Load(int3(tap, 0));
            if (value.a < 0.0 || abs(historyRoughness(value.a) - surface.roughness) > kRoughnessChange)
            {
                continue;
            }
            const float weight = exp(-2.0 * r * r) * packedWeight(surface, tap, distance);
            reflection += compress(value.rgb) * weight;
            weightSum += weight;
        }
    }
    return float4(expand(reflection / weightSum), own.a);
}

/// Bilinear upsample of the footprint filtered reflection kept on the surface by plane distance and normal
[shader("fragment")]
float4 compositeMain(VSOutput input) : SV_Target0
{
    const int2 pixel = int2(input.position.xy);
    const float3 scene = gSceneColour.Load(int3(pixel, 0)).rgb;
    const TracedPixel surface = tracedPixelAt(pixel);
    if (!surface.traced)
    {
        return float4(scene, 1.0);
    }

    uint width;
    uint height;
    gFiltered.GetDimensions(width, height);
    const int2 last = int2(int(width), int(height)) - 1;
    const float2 low = (float2(pixel) + 0.5) * 0.5 - 0.5;
    const int2 base = int2(floor(low));
    const float distance = length(gCamera.position - surface.position);

    float3 reflection = float3(0.0, 0.0, 0.0);
    float weightSum = 0.0;
    for (int y = 0; y <= 1; ++y)
    {
        for (int x = 0; x <= 1; ++x)
        {
            const int2 texel = base + int2(x, y);
            if (any(texel < int2(0, 0)) || any(texel > last))
            {
                continue;
            }
            const float4 value = gFiltered.Load(int3(texel, 0));
            if (value.a < 0.0)
            {
                continue;
            }
            const float2 offset = float2(texel) - low;
            const float weight =
                exp(-dot(offset, offset) / (2.0 * kBilinearSigma * kBilinearSigma)) * sourceWeight(surface, texel, distance);
            reflection += compress(value.rgb) * weight;
            weightSum += weight;
        }
    }

    // A thin surface the half resolution grid skipped this frame takes its best nearby texel instead of flipping to ambient
    if (weightSum <= 1e-4)
    {
        float best = 1e-3;
        for (int y = -1; y <= 2; ++y)
        {
            for (int x = -1; x <= 2; ++x)
            {
                const int2 texel = base + int2(x, y);
                if (any(texel < int2(0, 0)) || any(texel > last))
                {
                    continue;
                }
                const float4 value = gFiltered.Load(int3(texel, 0));
                if (value.a < 0.0 || abs(historyRoughness(value.a) - surface.roughness) > kRoughnessChange)
                {
                    continue;
                }
                const float score = sourceWeight(surface, texel, distance);
                if (score > best)
                {
                    best = score;
                    reflection = compress(value.rgb);
                    weightSum = 1.0;
                }
            }
        }
    }

    const float3 V = normalize(gCamera.position - surface.position);
    reflection = weightSum > 1e-4 ? expand(reflection / weightSum)
                                  : ambientSpecularRadiance(surface.position, reflect(-V, surface.normal), surface.roughness);

    const float NdotV = saturate(dot(surface.normal, V));
    const float3 F0 = lerp(float3(0.04, 0.04, 0.04), surface.albedo, surface.metallic);
    const float3 fresnel = fresnelSchlickRoughness(NdotV, F0, surface.roughness);
    const float2 split = environmentBRDF(surface.roughness, NdotV);
    // Quantized like the voxel_lighting copy so both sides of the handover agree
    const float share = tracedSpecularShare(surface.roughness, round(gParams.tracing.y * 255.0) / 255.0);
    return float4(scene + reflection * (fresnel * split.x + split.y) * (surface.reflectivity * share), 1.0);
}
