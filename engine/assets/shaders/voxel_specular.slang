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

// Mirrors TracedSpecularPass::Params
struct SpecularParams
{
    /// Jittered since depth was rasterized with it
    float4x4 inverseViewProjection;
    /// x reach in meters y max roughness z 1 with a valid history w sun reach in meters
    float4 tracing;
    /// x 1 when gPreviousColour holds last frame resolve
    float4 resolve;
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

/// Rounded normal with w kTracedMark where voxel lighting left the specular to this pass
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

/// Mirrors voxel_lighting.slang
static const float kTracedMark = 2.0;
static const float kVoxelSize = 0.1;
/// Ray origin offset off its face in voxels
static const float kOriginOffset = 0.6;

static const float kNoTrace = -1.0;
/// Below this a surface is a mirror and traces the reflection itself
static const float kMirrorRoughness = 0.05;
/// Trims the noisy GGX tail
static const float kLobeTrim = 0.75;
/// Meters off the plane of the hit face the visible surface may sit and still count as the same face
static const float kScreenPlane = 0.025;
/// History roughness further off than this belongs to another material
static const float kRoughnessChange = 0.05;
/// Frames of history a texel counts up to so 1 / count stops at the floor blend
static const float kMaxHistory = 15.0;
/// Trace distance of a ray that left the scene since its radiance came from a direction not a point
static const float kMissDistance = 60000.0;

/// Neighbour rays a texel reuses on a rotated golden angle spiral besides its own
static const uint kResolveTaps = 12u;
/// Spiral radius in half resolution texels from mirrors to the roughness cut
static const float kResolveRadiusMirror = 2.0;
static const float kResolveRadiusRough = 5.0;
/// Lobe over pdf a single ray may reach so one unlikely neighbour cannot take the texel
static const float kMaxRayWeight = 8.0;

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

/// False unless the pixel the hit projects to shows the same face
bool screenRadiance(float3 position, float3 normal, out float3 radiance)
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
    radiance = gSceneColour.Load(int3(pixel, 0)).rgb;
    const float4 motion = gMotion.Load(int3(pixel, 0));
    const float2 previousUv = (float2(pixel) + 0.5) / float2(float(width), float(height)) - (motion.w > 0.5 ? motion.xy : float2(0.0, 0.0));
    if (gParams.resolve.x > 0.5 && motion.z <= 0.5 && all(previousUv >= float2(0.0, 0.0)) && all(previousUv < float2(1.0, 1.0)))
    {
        radiance = gPreviousColour.Load(int3(int2(previousUv * float2(float(width), float(height))), 0)).rgb;
    }
    return true;
}

float3 hitRadiance(SceneHit hit, float3 origin, float3 direction, float coneSlope, float2 pixel, inout uint random)
{
    const float3 position = origin + direction * hit.t;
    const float3 normal = sceneHitNormal(hit, direction);
    float3 radiance;
    if (screenRadiance(position, normal, radiance))
    {
        return radiance;
    }

    const PaletteSample palette = samplePalette(gTraceVolumes[hit.volume].recordIndex, hit.material);
    float3 irradiance;
    if (!sampleRadianceCascades(position, normal, irradiance))
    {
        irradiance = gCamera.ambientColorIntensity.rgb * gCamera.ambientColorIntensity.a;
        if (gCamera.environmentParams.x >= 0.5)
        {
            irradiance += gIrradianceMap.SampleLevel(normal, 0.0).rgb;
        }
    }
    return palette.albedo * (palette.emissive + irradiance) + traceLocalLight(position, normal, palette.albedo, coneSlope, random) +
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

/// A miss returns the environment term lighting would have added
[shader("fragment")]
float4 traceMain(VSOutput input) : SV_Target0
{
    const int2 pixel = int2(input.position.xy) * 2;
    const TracedPixel surface = tracedPixelAt(pixel);
    if (!surface.traced)
    {
        return float4(0.0, 0.0, 0.0, kNoTrace);
    }

    // Half resolution coordinates since every other texel is not blue noise
    const float4 noise = blueNoise(input.position.xy, 160u);
    float pdf;
    const float3 L = rayDirection(surface, noise.xy, pdf);
    const float3 mirror = reflect(-normalize(gCamera.position - surface.position), surface.normal);
    if (dot(L, surface.face) <= 0.0)
    {
        return float4(ambientSpecularRadiance(surface.position, mirror, surface.roughness), kMissDistance);
    }

    // Rough lobes blur what they hit so their marches take a cone floor
    const float coneSlope = 0.25 * surface.roughness * surface.roughness;
    const float3 origin = surface.position + surface.face * (kOriginOffset * kVoxelSize);
    const SceneHit hit = traceScene(origin, L, 0.0, gParams.tracing.x, coneSlope);
    if (!hit.hit)
    {
        return float4(ambientSpecularRadiance(surface.position, mirror, surface.roughness), kMissDistance);
    }
    uint random = giHash(uint(pixel.x) | (uint(pixel.y) << 16)) ^ asuint(noise.z);
    return float4(hitRadiance(hit, origin, L, coneSlope, float2(pixel), random), hit.t);
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
    const float radius = lerp(kResolveRadiusMirror, kResolveRadiusRough, saturate(surface.roughness / max(gParams.tracing.y, 1e-3)));
    const float angle = blueNoise(input.position.xy, 170u).x * 6.28318531;

    float3 sum = float3(0.0, 0.0, 0.0);
    float weightSum = 0.0;
    for (uint i = 0u; i <= kResolveTaps; ++i)
    {
        const float tapAngle = angle + float(i) * 2.39996323;
        const float2 offset = i == 0u ? float2(0.0, 0.0) : radius * sqrt((float(i) - 0.5) / float(kResolveTaps)) * float2(cos(tapAngle), sin(tapAngle));
        const int2 tap = clamp(texel + int2(round(offset)), int2(0, 0), last);
        if (i > 0u && all(tap == texel))
        {
            continue;
        }
        const float4 hit = gTrace.Load(int3(tap, 0));
        if (hit.a < 0.0)
        {
            continue;
        }
        const TracedPixel neighbour = i == 0u ? surface : tracedPixelAt(tap * 2);
        if (!neighbour.traced)
        {
            continue;
        }
        // A neighbour on another surface traced a ray this texel could not have
        const float geometry = planeWeight(surface, neighbour.position, distance) * saturate(dot(neighbour.normal, N) * 4.0 - 3.0);
        if (geometry <= 1e-3)
        {
            continue;
        }

        float pdf;
        const float3 sampled = rayDirection(neighbour, blueNoise(float2(tap), 160u).xy, pdf);
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
    if (weightSum <= 1e-6)
    {
        return float4(own.rgb, packed);
    }
    return float4(expand(sum / weightSum), packed);
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
    float3 mean = float3(0.0, 0.0, 0.0);
    float3 meanSquared = float3(0.0, 0.0, 0.0);
    float count = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const float4 value = gResolved.Load(int3(clamp(texel + int2(x, y), int2(0, 0), last), 0));
            if (value.a < 0.0)
            {
                continue;
            }
            const float3 mapped = compress(value.rgb);
            mean += mapped;
            meanSquared += mapped * mapped;
            count += 1.0;
        }
    }
    mean /= count;
    const float3 sigma = sqrt(max(meanSquared / count - mean * mean, float3(0.0, 0.0, 0.0)));

    const float4 motion = gMotion.Load(int3(texel * 2, 0));
    const float2 uv = (float2(texel) + 0.5) / float2(float(width), float(height));
    const float2 previousUv = uv - (motion.w > 0.5 ? motion.xy : float2(0.0, 0.0));
    if (motion.z > 0.5 || any(previousUv < float2(0.0, 0.0)) || any(previousUv > float2(1.0, 1.0)))
    {
        return current;
    }
    const float4 history = gHistory.SampleLevel(previousUv, 0.0);
    if (history.a < 0.0 || abs(historyRoughness(history.a) - roughness) > kRoughnessChange)
    {
        return current;
    }

    // A young history trusts the box less since it is still mostly noise
    const float frames = historyFrames(history.a) + 1.0;
    const float3 clipped = clamp(compress(history.rgb), mean - 2.0 * sigma, mean + 2.0 * sigma);
    const float floorBlend = lerp(0.2, 0.04, saturate(roughness / max(gParams.tracing.y, 1e-3)));
    const float blend = max(1.0 / (frames + 1.0), floorBlend);
    return float4(expand(lerp(clipped, compress(current.rgb), blend)), packHistory(frames, roughness));
}

/// Half resolution texels a rough pixel gathers across on top of the bilinear footprint
static const float kFilterRadius = 2.0;

/// Gaussian widened with roughness and kept on the surface by plane distance and normal
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
    gTemporal.GetDimensions(width, height);
    const int2 last = int2(int(width), int(height)) - 1;
    const float2 low = (float2(pixel) + 0.5) * 0.5 - 0.5;
    const int2 base = int2(floor(low));
    const float distance = length(gCamera.position - surface.position);
    const float spread = saturate(surface.roughness / max(gParams.tracing.y, 1e-3));
    const float sigma = 0.6 + kFilterRadius * spread;
    // Mirrors stay on the bilinear footprint and rough pixels reach two texels further
    const int reach = spread > 0.25 ? 2 : 0;

    float3 reflection = float3(0.0, 0.0, 0.0);
    float weightSum = 0.0;
    for (int y = -reach; y <= 1 + reach; ++y)
    {
        for (int x = -reach; x <= 1 + reach; ++x)
        {
            const int2 texel = base + int2(x, y);
            if (any(texel < int2(0, 0)) || any(texel > last))
            {
                continue;
            }
            const float4 value = gTemporal.Load(int3(texel, 0));
            if (value.a < 0.0)
            {
                continue;
            }
            const int2 source = texel * 2;
            const float3 sourceNormal = gSceneNormal.Load(int3(source, 0)).xyz;
            const float facing = dot(sourceNormal, sourceNormal) > 1e-4 ? saturate(dot(normalize(sourceNormal), surface.normal)) : 0.0;
            const float3 sourcePosition = worldAt(source, gDepth.Load(int3(source, 0)).r);
            const float2 offset = float2(texel) - low;
            const float weight =
                exp(-dot(offset, offset) / (2.0 * sigma * sigma)) * planeWeight(surface, sourcePosition, distance) * pow(facing, 16.0);
            reflection += compress(value.rgb) * weight;
            weightSum += weight;
        }
    }

    const float3 V = normalize(gCamera.position - surface.position);
    reflection = weightSum > 1e-4 ? expand(reflection / weightSum)
                                  : ambientSpecularRadiance(surface.position, reflect(-V, surface.normal), surface.roughness);

    const float NdotV = saturate(dot(surface.normal, V));
    const float3 F0 = lerp(float3(0.04, 0.04, 0.04), surface.albedo, surface.metallic);
    const float3 fresnel = fresnelSchlickRoughness(NdotV, F0, surface.roughness);
    const float2 split = environmentBRDF(surface.roughness, NdotV);
    return float4(scene + reflection * (fresnel * split.x + split.y) * surface.reflectivity, 1.0);
}
