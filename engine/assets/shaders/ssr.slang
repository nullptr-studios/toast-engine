// Stochastic SSR traced resolved and accumulated at half resolution then composited
import fullscreen;
import depth_convention;
import scene_normal;

struct SsrParams
{
    float4x4 viewProjection;
    float4x4 inverseViewProjection;
    /// xyz camera position w frame index for the noise
    float4 cameraPosition;
    // x intensity y max roughness z thickness w step count
    float4 tuning;
    // x reach in meters y render mode z 1 with a valid history
    float4 marching;
};

[[vk::push_constant]]
ConstantBuffer<SsrParams> gParams;

[[vk::binding(0, 0)]]
Sampler2D gSceneColor;

[[vk::binding(1, 0)]]
Sampler2D gDepth;

[[vk::binding(2, 0)]]
Sampler2D gNormalRoughness;

/// xy hit uv z pdf of the ray and w confidence with 0 a miss
[[vk::binding(3, 0)]]
Sampler2D gHits;

/// rgb reflection a confidence
[[vk::binding(4, 0)]]
Sampler2D gResolved;

[[vk::binding(5, 0)]]
Sampler2D gHistory;

[[vk::binding(6, 0)]]
Sampler2D gMotion;

[[vk::binding(7, 0)]]
Sampler2D gTemporal;

static const float kPi = 3.14159265;

/// Below this a surface is a mirror and gets one ray with no reuse
static const float kMirrorRoughness = 0.05;

/// Trims the noisy GGX tail
static const float kLobeTrim = 0.75;

static const float kTemporalBlend = 0.1;

static const uint kResolveTaps = 5u;
static const float2 kResolvePattern[5] = {
    float2(0.0, 0.0), float2(1.0, 0.3), float2(-0.3, 1.0), float2(-1.0, -0.3), float2(0.3, -1.0),
};

typedef FullscreenVSOutput VSOutput;

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VertexID)
{
    return fullscreenTriangle(vertexID);
}

float3 worldPositionFromDepth(float2 uv, float depth)
{
    float4 clip = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 world = mul(gParams.inverseViewProjection, clip);
    return world.xyz / world.w;
}

/// Clip w from the 1 / w row of the inverse view projection
float linearDepth(float2 uv, float depth)
{
    return 1.0 / max(dot(gParams.inverseViewProjection[3], float4(uv * 2.0 - 1.0, depth, 1.0)), 1e-8);
}

bool insideScreen(float2 uv)
{
    return all(uv >= float2(0.0, 0.0)) && all(uv <= float2(1.0, 1.0));
}

float screenEdgeFade(float2 uv)
{
    float2 fromEdge = min(uv, float2(1.0, 1.0) - uv);
    float fade = saturate(min(fromEdge.x, fromEdge.y) / 0.1);
    return fade * fade;
}

float luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// Interleaved gradient noise
float noiseAt(float2 pixel)
{
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

/// Walks R2 per frame
float2 noise2(float2 pixel)
{
    const float frame = gParams.cameraPosition.w;
    return frac(float2(noiseAt(pixel), noiseAt(pixel.yx + float2(47.0, 17.0))) + frame * float2(0.7548776662, 0.5698402910));
}

float ggxD(float NdotH, float alpha)
{
    const float a2 = alpha * alpha;
    const float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(kPi * d * d, 1e-8);
}

float smithG1(float NdotX, float alpha)
{
    const float a2 = alpha * alpha;
    return 2.0 * NdotX / max(NdotX + sqrt(a2 + (1.0 - a2) * NdotX * NdotX), 1e-8);
}

/// Heitz 2018 visible normal sampling in a frame with z up
float3 sampleVisibleNormal(float3 view, float alpha, float2 u)
{
    const float3 stretched = normalize(float3(alpha * view.x, alpha * view.y, view.z));
    const float lengthSquared = stretched.x * stretched.x + stretched.y * stretched.y;
    const float3 t1 = lengthSquared > 0.0 ? float3(-stretched.y, stretched.x, 0.0) * rsqrt(lengthSquared) : float3(1.0, 0.0, 0.0);
    const float3 t2 = cross(stretched, t1);
    const float r = sqrt(u.x);
    const float phi = 2.0 * kPi * u.y;
    const float p1 = r * cos(phi);
    const float s = 0.5 * (1.0 + stretched.z);
    const float p2 = (1.0 - s) * sqrt(max(1.0 - p1 * p1, 0.0)) + s * r * sin(phi);
    const float3 h = p1 * t1 + p2 * t2 + sqrt(max(1.0 - p1 * p1 - p2 * p2, 0.0)) * stretched;
    return normalize(float3(alpha * h.x, alpha * h.y, max(h.z, 0.0)));
}

/// Of a direction sampled through sampleVisibleNormal
float visibleNormalPdf(float NdotV, float NdotH, float alpha)
{
    return smithG1(NdotV, alpha) * ggxD(NdotH, alpha) / max(4.0 * NdotV, 1e-8);
}

struct SurfaceSample
{
    bool valid;
    float3 position;
    float3 normal;
    float roughness;
    float f0;
    float depth;
    float linearDepth;
};

SurfaceSample surfaceAt(float2 uv)
{
    SurfaceSample surface;
    surface.valid = false;
    surface.position = float3(0.0, 0.0, 0.0);
    surface.normal = float3(0.0, 0.0, 1.0);
    surface.roughness = 1.0;
    surface.f0 = 0.0;
    surface.depth = gDepth.SampleLevel(uv, 0.0).r;
    surface.linearDepth = 0.0;

    const float4 encoded = gNormalRoughness.SampleLevel(uv, 0.0);
    if (dot(encoded.xyz, encoded.xyz) < 1e-4 || isFarDepth(surface.depth))
    {
        return surface;
    }
    surface.valid = true;
    surface.position = worldPositionFromDepth(uv, surface.depth);
    surface.normal = normalize(encoded.xyz);
    surface.roughness = encoded.w;
    surface.f0 = sceneNormalF0(encoded);
    surface.linearDepth = linearDepth(uv, surface.depth);
    return surface;
}

bool marchScreen(float3 origin, float3 direction, float2 jitter, out float2 hitUv, out float travelled)
{
    hitUv = float2(0.0, 0.0);
    travelled = 0.0;

    uint width;
    uint height;
    gDepth.GetDimensions(width, height);
    const float2 size = float2(float(width), float(height));

    float reach = gParams.marching.x;
    const float4 start = mul(gParams.viewProjection, float4(origin, 1.0));
    float4 end = mul(gParams.viewProjection, float4(origin + direction * reach, 1.0));

    // Stops short of the near plane so 1 / w stays finite
    const float minW = 1e-3;
    if (end.w < minW)
    {
        reach *= (start.w - minW) / max(start.w - end.w, 1e-6);
        end = mul(gParams.viewProjection, float4(origin + direction * reach, 1.0));
    }

    const float k0 = 1.0 / start.w;
    const float k1 = 1.0 / end.w;
    const float2 uv0 = start.xy * k0 * 0.5 + 0.5;
    const float2 uv1 = end.xy * k1 * 0.5 + 0.5;

    // At most one sample per pixel the ray crosses
    const float pixels = max(abs(uv1.x - uv0.x) * size.x, abs(uv1.y - uv0.y) * size.y);
    const uint steps = clamp(uint(pixels), 1u, uint(gParams.tuning.w));

    float previousT = 0.0;
    float previousRayDepth = start.w;
    for (uint i = 0u; i < steps; ++i)
    {
        // Squared to crowd samples near the surface
        const float x = (float(i) + jitter.x) / float(steps);
        const float t = x * x;
        const float2 uv = lerp(uv0, uv1, t);
        if (!insideScreen(uv))
        {
            return false;
        }

        const float rayDepth = 1.0 / lerp(k0, k1, t);
        const float depth = gDepth.SampleLevel(uv, 0.0).r;
        if (!isFarDepth(depth))
        {
            const float sceneDepth = linearDepth(uv, depth);
            const float behind = rayDepth - sceneDepth;
            // Never thinner than one step
            const float thickness = max(gParams.tuning.z * (1.0 + 0.025 * sceneDepth), 1.5 * abs(rayDepth - previousRayDepth));
            if (behind > 0.0 && behind < thickness)
            {
                float lo = previousT;
                float hi = t;
                for (uint j = 0u; j < 5u; ++j)
                {
                    const float mid = 0.5 * (lo + hi);
                    const float2 midUv = lerp(uv0, uv1, mid);
                    const float midDepth = gDepth.SampleLevel(midUv, 0.0).r;
                    if (!isFarDepth(midDepth) && 1.0 / lerp(k0, k1, mid) > linearDepth(midUv, midDepth))
                    {
                        hi = mid;
                    }
                    else
                    {
                        lo = mid;
                    }
                }
                hitUv = lerp(uv0, uv1, hi);
                travelled = hi;
                return true;
            }
        }
        previousT = t;
        previousRayDepth = rayDepth;
    }
    return false;
}

[shader("fragment")]
float4 traceMain(VSOutput input) : SV_Target0
{
    const SurfaceSample surface = surfaceAt(input.uv);
    if (!surface.valid || surface.roughness > gParams.tuning.y)
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    const float3 N = surface.normal;
    const float3 V = normalize(gParams.cameraPosition.xyz - surface.position);
    const float NdotV = saturate(dot(N, V));
    const float2 u = noise2(input.position.xy);

    float3 L = reflect(-V, N);
    float pdf = 1.0;
    if (surface.roughness > kMirrorRoughness)
    {
        const float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
        const float3 tangent = normalize(cross(up, N));
        const float3 bitangent = cross(N, tangent);
        const float3 localView = float3(dot(V, tangent), dot(V, bitangent), NdotV);
        const float alpha = surface.roughness * surface.roughness;
        const float3 h = sampleVisibleNormal(localView, alpha, float2(u.x * kLobeTrim, u.y));
        const float3 H = normalize(tangent * h.x + bitangent * h.y + N * h.z);
        L = reflect(-V, H);
        pdf = visibleNormalPdf(max(NdotV, 1e-4), saturate(dot(N, H)), alpha);
    }
    if (dot(L, N) <= 0.0 || dot(L, V) > 0.999)
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    const float3 origin = surface.position + N * max(0.002 * surface.linearDepth, 0.01);

    float2 hitUv;
    float travelled;
    if (!marchScreen(origin, L, u, hitUv, travelled))
    {
        return float4(0.0, 0.0, pdf, 0.0);
    }

    // A back face of what it hit means the ray went behind it
    const float4 hitEncoded = gNormalRoughness.SampleLevel(hitUv, 0.0);
    const float facing = dot(hitEncoded.xyz, hitEncoded.xyz) > 1e-4 ? -dot(normalize(hitEncoded.xyz), L) : 1.0;
    const float confidence = screenEdgeFade(hitUv) * (1.0 - smoothstep(0.7, 1.0, travelled)) * step(0.0, facing);
    return float4(hitUv, pdf, confidence);
}

[shader("fragment")]
float4 resolveMain(VSOutput input) : SV_Target0
{
    const SurfaceSample surface = surfaceAt(input.uv);
    if (!surface.valid || surface.roughness > gParams.tuning.y)
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    uint width;
    uint height;
    gHits.GetDimensions(width, height);
    const float2 texel = 1.0 / float2(float(width), float(height));

    const float3 N = surface.normal;
    const float3 V = normalize(gParams.cameraPosition.xyz - surface.position);
    const float NdotV = max(saturate(dot(N, V)), 1e-4);
    const float alpha = max(surface.roughness * surface.roughness, 1e-3);

    const bool mirror = surface.roughness <= kMirrorRoughness;
    const uint taps = mirror ? 1u : kResolveTaps;
    const float radius = lerp(1.0, 4.0, saturate(surface.roughness / max(gParams.tuning.y, 1e-4)));
    const float angle = noise2(input.position.xy + 31.0).x * 2.0 * kPi;
    const float2x2 rotation = float2x2(cos(angle), -sin(angle), sin(angle), cos(angle));

    // Tonemapped so one bright hit does not speckle
    float3 colorSum = float3(0.0, 0.0, 0.0);
    float brdfSum = 0.0;
    float confidenceSum = 0.0;
    float geometrySum = 0.0;
    for (uint i = 0u; i < taps; ++i)
    {
        const float2 uv = input.uv + mul(rotation, kResolvePattern[i]) * radius * texel;
        const float4 hit = gHits.SampleLevel(uv, 0.0);

        // A neighbour on another surface traced a ray this pixel could not have
        float geometry = 1.0;
        if (i > 0u)
        {
            const SurfaceSample neighbour = surfaceAt(uv);
            if (!neighbour.valid)
            {
                continue;
            }
            geometry = exp(-abs(neighbour.linearDepth - surface.linearDepth) / (0.02 * surface.linearDepth + 1e-3)) *
                       saturate(dot(neighbour.normal, N) * 4.0 - 3.0);
        }
        if (geometry <= 1e-3)
        {
            continue;
        }
        geometrySum += geometry;
        if (hit.w <= 0.0)
        {
            continue;
        }

        const float hitDepth = gDepth.SampleLevel(hit.xy, 0.0).r;
        const float3 L = normalize(worldPositionFromDepth(hit.xy, hitDepth) - surface.position);
        const float NdotL = dot(N, L);
        if (NdotL <= 0.0)
        {
            continue;
        }

        float brdf = 1.0;
        if (!mirror)
        {
            const float3 H = normalize(V + L);
            brdf = ggxD(saturate(dot(N, H)), alpha) * smithG1(NdotV, alpha) * smithG1(NdotL, alpha) / max(4.0 * NdotV, 1e-4) /
                   max(hit.z, 1e-4);
        }
        brdf *= geometry;

        const float3 color = gSceneColor.SampleLevel(hit.xy, 0.0).rgb;
        colorSum += brdf * hit.w * color / (1.0 + luminance(color));
        brdfSum += brdf * hit.w;
        confidenceSum += geometry * hit.w;
    }

    if (brdfSum <= 1e-6 || geometrySum <= 1e-6)
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    const float3 mapped = colorSum / brdfSum;
    return float4(mapped / max(1.0 - luminance(mapped), 1e-3), saturate(confidenceSum / geometrySum));
}

[shader("fragment")]
float4 temporalMain(VSOutput input) : SV_Target0
{
    uint width;
    uint height;
    gResolved.GetDimensions(width, height);
    const float2 texel = 1.0 / float2(float(width), float(height));

    const float4 current = gResolved.SampleLevel(input.uv, 0.0);

    float4 mean = float4(0.0, 0.0, 0.0, 0.0);
    float4 meanSquared = float4(0.0, 0.0, 0.0, 0.0);
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const float4 value = gResolved.SampleLevel(input.uv + float2(float(x), float(y)) * texel, 0.0);
            mean += value;
            meanSquared += value * value;
        }
    }
    mean /= 9.0;
    const float4 sigma = sqrt(max(meanSquared / 9.0 - mean * mean, float4(0.0, 0.0, 0.0, 0.0)));

    const float4 motion = gMotion.SampleLevel(input.uv, 0.0);
    const float2 previousUv = input.uv - (motion.w > 0.5 ? motion.xy : float2(0.0, 0.0));
    // Reactive pixels drop their history
    if (gParams.marching.z < 0.5 || !insideScreen(previousUv) || motion.z > 0.5)
    {
        return current;
    }

    const float4 history = clamp(gHistory.SampleLevel(previousUv, 0.0), mean - 1.25 * sigma, mean + 1.25 * sigma);
    return lerp(history, current, kTemporalBlend);
}

/// Karis analytic fit of the split sum environment BRDF as scale and bias on F0
float2 environmentBrdfApprox(float roughness, float NdotV)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    const float4 r = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}

[shader("fragment")]
float4 compositeMain(VSOutput input) : SV_Target0
{
    const float3 sceneColor = gSceneColor.SampleLevel(input.uv, 0.0).rgb;
    const float4 encoded = gNormalRoughness.SampleLevel(input.uv, 0.0);
    const uint renderMode = uint(gParams.marching.y);

    if (renderMode == 12)
    {
        if (dot(encoded.xyz, encoded.xyz) < 1e-4)
        {
            return float4(0.0, 0.0, 0.0, 1.0);
        }
        return float4(normalize(encoded.xyz) * 0.5 + 0.5, 1.0);
    }

    if (renderMode == 13)
    {
        return float4(encoded.www, 1.0);
    }

    // Voxel debug views 20 to 24 so a floor never reflects a legend colour
    if (renderMode >= 20 && renderMode <= 24)
    {
        return float4(sceneColor, 1.0);
    }

    const bool ssrOnly = (renderMode == 14);
    const float3 passthrough = ssrOnly ? float3(0.0, 0.0, 0.0) : sceneColor;

    const SurfaceSample surface = surfaceAt(input.uv);
    const float maxRoughness = gParams.tuning.y;
    if (!surface.valid || surface.roughness > maxRoughness)
    {
        return float4(passthrough, 1.0);
    }

    // Depth aware bilinear
    uint width;
    uint height;
    gTemporal.GetDimensions(width, height);
    const float2 lowSize = float2(float(width), float(height));
    const float2 position = input.uv * lowSize - 0.5;
    const float2 base = floor(position);
    const float2 fraction = position - base;

    float4 reflection = float4(0.0, 0.0, 0.0, 0.0);
    float weightSum = 0.0;
    for (int y = 0; y < 2; ++y)
    {
        for (int x = 0; x < 2; ++x)
        {
            const float2 offset = float2(float(x), float(y));
            const float2 uv = (base + offset + 0.5) / lowSize;
            const float2 bilinear = lerp(1.0 - fraction, fraction, offset);
            const float depth = gDepth.SampleLevel(uv, 0.0).r;
            const float texelDepth = isFarDepth(depth) ? 1e6 : linearDepth(uv, depth);
            const float weight = bilinear.x * bilinear.y *
                                 exp(-abs(texelDepth - surface.linearDepth) / (0.02 * surface.linearDepth + 1e-3)) + 1e-5 * bilinear.x * bilinear.y;
            reflection += gTemporal.SampleLevel(uv, 0.0) * weight;
            weightSum += weight;
        }
    }
    reflection /= max(weightSum, 1e-6);

    if (ssrOnly)
    {
        return float4(reflection.rgb * reflection.a, 1.0);
    }

    const float3 V = normalize(gParams.cameraPosition.xyz - surface.position);
    const float2 split = environmentBrdfApprox(surface.roughness, saturate(dot(surface.normal, V)));
    const float fresnel = surface.f0 * split.x + split.y;
    const float roughnessFade = 1.0 - smoothstep(0.6 * maxRoughness, maxRoughness, surface.roughness);

    const float weight = reflection.a * fresnel * roughnessFade * gParams.tuning.x;
    return float4(sceneColor + reflection.rgb * weight, 1.0);
}
