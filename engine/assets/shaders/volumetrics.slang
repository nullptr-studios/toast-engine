// Set 0 is the engine scene and set 1 the voxel storage with the scene trace
import fullscreen;
import depth_convention;
import voxel_dda;
import lighting;
import gi_common;
import gi_sample;
import scene_trace;
import trace_shading;
import fog_media;

// Mirrors VolumetricPass::Params
struct VolumetricParams
{
    /// Jittered since depth was rasterized with it
    float4x4 inverseViewProjection;
    /// x extinction per meter at the base height y base height z height falloff per meter w anisotropy
    float4 medium;
    /// rgb scattering albedo a ambient scale
    float4 albedo;
    /// x march distance y steps z 1 with a valid history w fog volumes
    float4 marching;
    /// x full resolution pixels per texel y local lights per step z 1 when local lights cast shadows w fog volume distance
    float4 quality;
};

[[vk::push_constant]]
ConstantBuffer<VolumetricParams> gParams;

[[vk::binding(0, 2)]]
Sampler2D gSceneColour;

[[vk::binding(1, 2)]]
Sampler2D gDepth;

/// xy uv motion z reactive w 1 when written
[[vk::binding(2, 2)]]
Sampler2D gMotion;

/// rgb in-scattered radiance a transmittance
[[vk::binding(3, 2)]]
Sampler2D gScatter;

[[vk::binding(4, 2)]]
Sampler2D gHistory;

[[vk::binding(5, 2)]]
Sampler2D gTemporal;

/// Mean radiance at each fog volume centre then the camera with w the probe key
[[vk::binding(6, 2)]]
RWStructuredBuffer<float4> gFogAmbient;

[[vk::binding(7, 2)]]
StructuredBuffer<float4> gPreviousFogAmbient;

/// Cone floor of fog shadow rays since shafts are soft
static const float kFogShadowCone = 0.02;

/// Rays and meters each ambient probe traces
static const uint kAmbientRays = 64u;
static const float kAmbientReach = 40.0;
/// New probe share per frame so one ray at a lamp cannot flash a volume
static const float kAmbientBlend = 0.1;
/// Mirrors VulkanRenderer::k_max_fog_volumes plus the camera
static const uint kAmbientProbes = 33u;
static const float kTemporalBlend = 0.1;

/// Meters per scatter step until the step setting caps the count
static const float kFogStepMeters = 0.5;
static const uint kFogMinSteps = 4u;
/// Marched light shadows trace on one step in this many, rotated over 2 by 2 texels and frames
static const uint kMarchedInterleave = 4u;

typedef FullscreenVSOutput VSOutput;

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    return fullscreenTriangle(vertexId);
}

/// Height fog stops at its own distance and fog volumes at the renderer setting
float marchLimit()
{
    const float height = gParams.medium.x > 0.0 ? gParams.marching.x : 0.0;
    const float volumes = gParams.marching.w > 0.0 ? gParams.quality.w : 0.0;
    return max(height, volumes);
}

float2 fullSize()
{
    uint width;
    uint height;
    gDepth.GetDimensions(width, height);
    return float2(float(width), float(height));
}

/// Where the view ray of a full resolution pixel ends and how far that is
float3 rayEnd(int2 pixel, out float distance)
{
    const float2 uv = (float2(pixel) + 0.5) / fullSize();
    const float depth = gDepth.Load(int3(pixel, 0)).r;
    const float4 farClip = mul(gParams.inverseViewProjection, float4(uv * 2.0 - 1.0, isFarDepth(depth) ? kNearDepth : depth, 1.0));
    const float3 point = farClip.xyz / farClip.w;
    const float3 toPoint = point - gCamera.position;
    const float span = max(sqrt(dot(toPoint, toPoint)), 1e-4);
    distance = isFarDepth(depth) ? marchLimit() : min(span, marchLimit());
    return gCamera.position + toPoint / span * distance;
}

float henyeyGreenstein(float cosine, float g)
{
    const float g2 = g * g;
    return (1.0 - g2) / (4.0 * kPi * pow(max(1.0 + g2 - 2.0 * g * cosine, 1e-4), 1.5));
}

struct Medium
{
    float extinction;
    float3 scattering;
    float3 emission;
    /// Weighted by the scattering of each medium
    float anisotropy;
    /// Scattering times the ambient probe of each medium
    float3 ambientScatter;
};

float latticeHash(float3 cell)
{
    const uint3 u = uint3(int3(cell) + 32768);
    uint h = u.x * 73856093u ^ u.y * 19349663u ^ u.z * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return float(h & 0xffffu) / 65535.0;
}

float valueNoise(float3 position)
{
    const float3 cell = floor(position);
    const float3 f = position - cell;
    const float3 s = f * f * (3.0 - 2.0 * f);
    const float x00 = lerp(latticeHash(cell), latticeHash(cell + float3(1, 0, 0)), s.x);
    const float x10 = lerp(latticeHash(cell + float3(0, 1, 0)), latticeHash(cell + float3(1, 1, 0)), s.x);
    const float x01 = lerp(latticeHash(cell + float3(0, 0, 1)), latticeHash(cell + float3(1, 0, 1)), s.x);
    const float x11 = lerp(latticeHash(cell + float3(0, 1, 1)), latticeHash(cell + float3(1, 1, 1)), s.x);
    return lerp(lerp(x00, x10, s.y), lerp(x01, x11, s.y), s.z);
}

/// Three octaves in 0 to 1
float fogNoise(float3 position)
{
    return 0.5714 * valueNoise(position) + 0.2857 * valueNoise(position * 2.03) + 0.1429 * valueNoise(position * 4.07);
}

Medium mediumAt(float3 position, float along)
{
    Medium medium;
    medium.extinction = along <= gParams.marching.x
                            ? gParams.medium.x * exp(-clamp(gParams.medium.z * (position.y - gParams.medium.y), -20.0, 20.0))
                            : 0.0;
    medium.scattering = medium.extinction * gParams.albedo.rgb;
    medium.emission = float3(0.0, 0.0, 0.0);
    float weight = medium.extinction;
    float anisotropy = medium.extinction * gParams.medium.w;
    const uint count = uint(gParams.marching.w);
    medium.ambientScatter = medium.scattering * gParams.albedo.a * gFogAmbient[count].rgb;
    for (uint v = 0u; v < count; ++v)
    {
        const FogVolume volume = gFogVolumes[v];
        const float3 local = mul(volume.worldToLocal, float4(position, 1.0)).xyz;
        const float3 inside = volume.extentsFade.xyz - abs(local);
        if (any(inside <= 0.0))
        {
            continue;
        }
        const float edge = saturate(min(inside.x, min(inside.y, inside.z)) / max(volume.extentsFade.w, 1e-3));
        float density = volume.albedoDensity.a * edge * edge;
        if (volume.emissionNoise.a > 0.0)
        {
            const float noise = fogNoise((position - volume.windScale.xyz * gCamera.time) / volume.windScale.w);
            density *= saturate(lerp(1.0, 2.0 * noise, volume.emissionNoise.a));
        }
        medium.extinction += density;
        medium.scattering += density * volume.albedoDensity.rgb;
        medium.emission += density * volume.emissionNoise.rgb;
        weight += density;
        anisotropy += density * volume.phase.x;
        medium.ambientScatter += density * volume.albedoDensity.rgb * volume.phase.y * gFogAmbient[v].rgb;
    }
    medium.anisotropy = weight > 0.0 ? anisotropy / weight : 0.0;
    return medium;
}

/// Span across the fog volumes so a level without height fog marches only inside them
bool volumeSpan(float3 origin, float3 direction, float maxDistance, out float near, out float far)
{
    near = maxDistance;
    far = 0.0;
    const uint count = uint(gParams.marching.w);
    for (uint v = 0u; v < count; ++v)
    {
        const FogVolume volume = gFogVolumes[v];
        const float3 localOrigin = mul(volume.worldToLocal, float4(origin, 1.0)).xyz;
        const float3 localDirection = mul(volume.worldToLocal, float4(direction, 0.0)).xyz;
        const float3 inverse = 1.0 / select(abs(localDirection) > 1e-8, localDirection, float3(1e-8, 1e-8, 1e-8));
        const float3 t0 = (-volume.extentsFade.xyz - localOrigin) * inverse;
        const float3 t1 = (volume.extentsFade.xyz - localOrigin) * inverse;
        const float3 low = min(t0, t1);
        const float3 high = max(t0, t1);
        const float enter = max(max(low.x, low.y), max(low.z, 0.0));
        const float exit = min(min(high.x, high.y), min(high.z, maxDistance));
        if (enter < exit)
        {
            near = min(near, enter);
            far = max(far, exit);
        }
    }
    return near < far;
}

float3 skyRadiance(float3 direction)
{
    const float3 ambient = gCamera.ambientColorIntensity.rgb * gCamera.ambientColorIntensity.a;
    return gCamera.environmentParams.x >= 0.5 ? ambient + gPrefilteredEnv.SampleLevel(direction, 0.0).rgb : ambient;
}

/// Shaded like a GI off screen hit
float3 probeHitRadiance(SceneHit hit, float3 origin, float3 direction, inout uint random)
{
    const float3 position = origin + direction * hit.t;
    const float3 normal = sceneHitNormal(hit, direction);
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
    return palette.albedo * (palette.emissive + irradiance) + traceLocalLight(position, normal, palette.albedo, kFogShadowCone, random) +
           traceSunLight(position, normal, palette.albedo, kFogShadowCone, gCamera.fogReach.z, float2(0.0, 0.0));
}

groupshared float3 gsAmbient[kAmbientRays];

/// One group per fog volume plus the camera so fog is lit by its room
[shader("compute")]
[numthreads(kAmbientRays, 1, 1)]
void ambientMain(uint3 group : SV_GroupID, uint index : SV_GroupIndex)
{
    const uint probe = group.x;
    const uint count = uint(gParams.marching.w);
    const float3 origin = probe < count ? gFogVolumes[probe].centre.xyz : gCamera.position;

    // Fibonacci sphere turned per frame
    const uint frame = uint(gCamera.noiseParams.z);
    const float turn = float(giHash(probe ^ giHash(frame)) & 0xffffu) / 65536.0 * 6.28318531;
    const float height = 1.0 - (2.0 * float(index) + 1.0) / float(kAmbientRays);
    const float ring = sqrt(saturate(1.0 - height * height));
    const float angle = float(index) * 2.39996323 + turn;
    const float3 direction = float3(ring * cos(angle), height, ring * sin(angle));

    uint random = giHash(index ^ giHash(probe ^ giHash(frame ^ 0x68bc21ebu)));
    const SceneHit hit = traceScene(origin, direction, 0.0, kAmbientReach, kFogShadowCone);
    // Tonemapped before the sum so one bright ray cannot carry the probe
    float3 radiance = hit.hit ? probeHitRadiance(hit, origin, direction, random) : skyRadiance(direction);
    radiance = all(isfinite(radiance)) ? max(radiance, float3(0.0, 0.0, 0.0)) : float3(0.0, 0.0, 0.0);
    gsAmbient[index] = radiance / (1.0 + dot(radiance, float3(0.2126, 0.7152, 0.0722)));
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = kAmbientRays / 2u; stride > 0u; stride >>= 1u)
    {
        if (index < stride)
        {
            gsAmbient[index] += gsAmbient[index + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (index == 0u)
    {
        const float3 mapped = gsAmbient[0] / float(kAmbientRays);
        const float3 current = mapped / max(1.0 - dot(mapped, float3(0.2126, 0.7152, 0.0722)), 1e-3);
        // Keyed by centre cell since the volume order shuffles
        const uint key = probe < count ? giHash(uint(int(floor(origin.x * 2.0))) ^ giHash(uint(int(floor(origin.y * 2.0))) ^
                                                giHash(uint(int(floor(origin.z * 2.0))))))
                                       : 0xffffffffu;
        float3 blended = current;
        if (gParams.marching.z > 0.5)
        {
            for (uint previous = 0u; previous < kAmbientProbes; ++previous)
            {
                const float4 stored = gPreviousFogAmbient[previous];
                if (asuint(stored.w) == key)
                {
                    blended = lerp(stored.rgb, current, kAmbientBlend);
                    break;
                }
            }
        }
        gFogAmbient[probe] = float4(blended, asfloat(key));
    }
}

/// One marched light per step traces a ray since none has a shadow map
struct MarchedPick
{
    float total;
    float weight;
    float3 light;
    float3 segment;
};

void offerMarched(inout MarchedPick pick, float3 light, float3 segment, inout uint random)
{
    const float weight = dot(light, float3(0.2126, 0.7152, 0.0722));
    if (weight <= 0.0)
    {
        return;
    }
    pick.total += weight;
    if (giRandom(random) * pick.total < weight)
    {
        pick.weight = weight;
        pick.light = light;
        pick.segment = segment;
    }
}

/// Sun and froxel lights toward the camera with marched ones only when @p traceMarched
float3 directInScatter(float3 position, float3 toCamera, float2 pixel, float g, bool traceMarched, inout uint random)
{
    const bool shadowed = gParams.quality.z > 0.5;
    MarchedPick pick;
    pick.total = 0.0;
    pick.weight = 0.0;
    pick.light = float3(0.0, 0.0, 0.0);
    pick.segment = float3(0.0, 0.0, 0.0);

    float3 light = float3(0.0, 0.0, 0.0);
    const uint casterIndex = uint(gCamera.shadowParams.x) - 1u;
    for (uint di = 0u; di < gCamera.directionalLightCountPad.x; ++di)
    {
        const DirectionalLightGpu sun = gCamera.directionalLights[di];
        const float3 L = normalize(-sun.direction.xyz);
        const float3 sunLight = sun.colorIntensity.rgb * sun.colorIntensity.a * henyeyGreenstein(dot(-L, toCamera), g);
        if (gCamera.shadowParams.x >= 0.5 && di == casterIndex)
        {
            light += sunLight * volumeSunShadow(position);
        }
        else if (shadowed && sun.direction.w > 0.0)
        {
            if (traceMarched)
            {
                offerMarched(pick, sunLight, L * gCamera.fogReach.z, random);
            }
        }
        else
        {
            light += sunLight;
        }
    }

    const float viewZ = mul(gCamera.view, float4(position, 1.0)).z;
    const ClusterCell cluster = clusterCellAt(viewZ, pixel);
    if (cluster.index < cluster.count)
    {
        const uint stride = gClusterParams.clusterDims.w;
        const uint indexBase = cluster.index * stride;
        const uint count = min(min(cluster.lightCount, stride), uint(gParams.quality.y));
        for (uint li = 0u; li < count; ++li)
        {
            const GpuLight local = gLights[gLightIndexList[indexBase + li]];
            const float3 samplePoint = analyticLightPoint(local, position);
            float3 L;
            float distance;
            const float3 radiance = lightSampleRadiance(local, position, samplePoint, L, distance);
            if (all(radiance <= 0.0))
            {
                continue;
            }
            const float3 localLight = radiance * henyeyGreenstein(dot(-L, toCamera), g);
            if (!shadowed)
            {
                light += localLight;
            }
            else if (lightShadowMode(local) == kShadowModeMap)
            {
                light += localLight * volumeLocalShadow(local, position);
            }
            else if (traceMarched)
            {
                offerMarched(pick, localLight, samplePoint - position, random);
            }
        }
    }

    // Scaled up since only one step in kMarchedInterleave traces
    if (pick.weight > 0.0)
    {
        const float visibility = traceVisibility(position, pick.segment, kFogShadowCone, false).x;
        light += pick.light * visibility * (pick.total / pick.weight) * float(kMarchedInterleave);
    }
    return light;
}

/// Energy conserving integration of in-scattering over extinction
[shader("fragment")]
float4 scatterMain(VSOutput input) : SV_Target0
{
    const int2 pixel = int2(input.position.xy) * int(gParams.quality.x);
    float distance;
    const float3 end = rayEnd(pixel, distance);
    const float3 direction = (end - gCamera.position) / max(distance, 1e-4);
    const float3 toCamera = -direction;
    const float jitter = blueNoise(input.position.xy, 180u).x;

    float start = 0.0;
    float span = distance;
    if (gParams.medium.x <= 0.0)
    {
        float far;
        if (!volumeSpan(gCamera.position, direction, distance, start, far))
        {
            return float4(0.0, 0.0, 0.0, 1.0);
        }
        span = far - start;
    }

    // Short indoor rays take fewer steps
    const uint maxSteps = max(uint(gParams.marching.y), 1u);
    const uint steps = clamp(uint(ceil(span / kFogStepMeters)), min(kFogMinSteps, maxSteps), maxSteps);
    const uint2 texel = uint2(input.position.xy);
    const uint phase = (texel.x & 1u) + ((texel.y & 1u) << 1u) + uint(gCamera.noiseParams.z);

    uint random = giHash(uint(pixel.x) ^ giHash(uint(pixel.y) ^ giHash(uint(gCamera.noiseParams.z))));
    float3 scattered = float3(0.0, 0.0, 0.0);
    float transmittance = 1.0;
    for (uint i = 0u; i < steps; ++i)
    {
        // Squared so steps crowd near the camera where a shaft spans the most pixels
        const float near = start + span * (float(i) / float(steps)) * (float(i) / float(steps));
        const float far = start + span * (float(i + 1u) / float(steps)) * (float(i + 1u) / float(steps));
        const float along = lerp(near, far, jitter);
        const float3 position = gCamera.position + direction * along;
        const Medium medium = mediumAt(position, along);
        if (medium.extinction <= 1e-6)
        {
            continue;
        }
        const float3 inScatter =
            medium.scattering *
                directInScatter(position, toCamera, float2(pixel), medium.anisotropy, (i + phase) % kMarchedInterleave == 0u, random) +
            medium.ambientScatter +
            medium.emission;
        const float stepExtinction = exp(-medium.extinction * (far - near));
        scattered += transmittance * inScatter * (1.0 - stepExtinction) / medium.extinction;
        transmittance *= stepExtinction;
        if (transmittance < 1e-3)
        {
            break;
        }
    }
    return float4(scattered, transmittance);
}

[shader("fragment")]
float4 temporalMain(VSOutput input) : SV_Target0
{
    const int2 texel = int2(input.position.xy);
    const float4 current = gScatter.Load(int3(texel, 0));
    if (gParams.marching.z < 0.5)
    {
        return current;
    }

    uint width;
    uint height;
    gScatter.GetDimensions(width, height);
    const int2 last = int2(int(width), int(height)) - 1;
    float4 mean = float4(0.0, 0.0, 0.0, 0.0);
    float4 meanSquared = float4(0.0, 0.0, 0.0, 0.0);
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const float4 value = gScatter.Load(int3(clamp(texel + int2(x, y), int2(0, 0), last), 0));
            mean += value;
            meanSquared += value * value;
        }
    }
    mean /= 9.0;
    const float4 sigma = sqrt(max(meanSquared / 9.0 - mean * mean, float4(0.0, 0.0, 0.0, 0.0)));

    // The end point through the previous camera since fog belongs to no surface
    float distance;
    const float3 end = rayEnd(texel * int(gParams.quality.x), distance);
    const float4 previousClip = mul(gCamera.previousViewProjection, float4(end, 1.0));
    const float2 previousUv = previousClip.xy / max(previousClip.w, 1e-6) * 0.5 + 0.5;
    if (previousClip.w <= 0.0 || any(previousUv < float2(0.0, 0.0)) || any(previousUv > float2(1.0, 1.0)) ||
        gMotion.Load(int3(texel * int(gParams.quality.x), 0)).z > 0.5)
    {
        return current;
    }
    const float4 history = clamp(gHistory.SampleLevel(previousUv, 0.0), mean - 1.5 * sigma, mean + 1.5 * sigma);
    return lerp(history, current, kTemporalBlend);
}

/// Bilinear among texels whose rays ended about as far away
[shader("fragment")]
float4 compositeMain(VSOutput input) : SV_Target0
{
    const int2 pixel = int2(input.position.xy);
    const float3 scene = gSceneColour.Load(int3(pixel, 0)).rgb;
    float distance;
    rayEnd(pixel, distance);

    uint width;
    uint height;
    gTemporal.GetDimensions(width, height);
    const int2 last = int2(int(width), int(height)) - 1;
    const float2 low = (float2(pixel) + 0.5) / gParams.quality.x - 0.5;
    const int2 base = int2(floor(low));
    const float2 fraction = low - float2(base);

    float4 fog = float4(0.0, 0.0, 0.0, 0.0);
    float weightSum = 0.0;
    float4 nearest = float4(0.0, 0.0, 0.0, 1.0);
    float nearestError = 1e30;
    for (int y = 0; y < 2; ++y)
    {
        for (int x = 0; x < 2; ++x)
        {
            const int2 texel = clamp(base + int2(x, y), int2(0, 0), last);
            const float4 value = gTemporal.Load(int3(texel, 0));
            float texelDistance;
            rayEnd(texel * int(gParams.quality.x), texelDistance);
            const float error = abs(texelDistance - distance);
            const float2 bilinear = lerp(1.0 - fraction, fraction, float2(float(x), float(y)));
            const float weight = bilinear.x * bilinear.y * exp(-error / (0.05 * distance + 0.05));
            fog += value * weight;
            weightSum += weight;
            if (error < nearestError)
            {
                nearestError = error;
                nearest = value;
            }
        }
    }
    fog = weightSum > 1e-4 ? fog / weightSum : nearest;
    return float4(scene * fog.a + fog.rgb, 1.0);
}
