struct DirectionalLightGpu
{
    float4 direction;        // world-space, w unused
    float4 colorIntensity;   // rgb color, a intensity
};

struct CameraUBO
{
    float4x4 view;
    float4x4 projection;
    float4x4 viewProjection;

    float3 position;
    float time;

    float4 ambientColorIntensity;    // rgb color, a intensity; zero if there's no AmbientLight
    uint4 directionalLightCountPad;  // x = directionalLightCount, yzw unused padding
    DirectionalLightGpu directionalLights[4];
};

[[vk::binding(0,0)]]
ConstantBuffer<CameraUBO> gCamera;
[[vk::binding(0,1)]]
Sampler2D gAlbedoSampler;
[[vk::binding(1,1)]]
Sampler2D gNormalSampler;

struct ClusterParams
{
    float4x4 inverseProjection;
    uint4 clusterDims;         // x,y,z dims, w = maxLightsPerCluster
    float4 screenSizeNearFar;  // x,y screen dims, z near, w far
    uint lightCount;
    float3 _pad0;
};

struct GpuLight
{
    float4 worldPosRange;   // xyz world-space position, w = attenuation range
    float4 viewPosType;     // xyz view-space position (culling only), w = type (0=point,1=spot)
    float4 colorIntensity;  // rgb color, a intensity
    float4 directionPad;    // xyz world-space direction (spot only)
    float4 coneAngles;      // x = cos(outer), y = cos(inner)
};

[[vk::binding(0,2)]]
ConstantBuffer<ClusterParams> gClusterParams;
[[vk::binding(1,2)]]
StructuredBuffer<GpuLight> gLights;
[[vk::binding(2,2)]]
StructuredBuffer<uint> gClusterLightGrid;
[[vk::binding(3,2)]]
StructuredBuffer<uint> gLightIndexList;

struct PushConstants
{
    float4x4 model;
    float4 color;
    float4 pbrParams; // x = metallic, y = roughness
};

[[vk::push_constant]]
PushConstants gPush;

struct VSInput
{
    [[vk::location(0)]]
    float3 position : POSITION;

    [[vk::location(1)]]
    float3 normal : NORMAL;

    [[vk::location(2)]]
    float2 uv : TEXCOORD0;

    [[vk::location(3)]]
    float4 tangent : TANGENT;

    [[vk::location(4)]]
    float3 color : COLOR;
};

struct VSOutput
{
    float4 position : SV_Position;

    [[vk::location(0)]]
    float3 worldNormal : NORMAL;

    [[vk::location(1)]]
    float2 uv : TEXCOORD0;

    [[vk::location(2)]]
    float3 color : COLOR;

    [[vk::location(3)]]
    float3 worldTangent : TANGENT;

    [[vk::location(4)]]
    float tangentSign : TEXCOORD1;

    [[vk::location(5)]]
    float3 worldPosition : TEXCOORD2;

    [[vk::location(6)]]
    float viewSpaceZ : TEXCOORD3;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    VSOutput output;

    float4 worldPos =
        mul(
            gPush.model,
            float4(input.position, 1.0));

    output.position =
        mul(
            gCamera.viewProjection,
            worldPos);

    output.worldPosition = worldPos.xyz;
    output.viewSpaceZ = mul(gCamera.view, worldPos).z;

    output.worldNormal =
        normalize(
            mul(
                (float3x3)gPush.model,
                input.normal));

    output.worldTangent =
        normalize(
            mul(
                (float3x3)gPush.model,
                input.tangent.xyz));
    output.tangentSign = input.tangent.w;

    output.uv =
        input.uv;

    output.color = input.color;

    return output;
}

static const float kPi = 3.14159265359;

// Trowbridge-Reitz GGX normal distribution
float distributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = kPi * denom * denom;
    return a2 / max(denom, 0.0000001);
}

// Schlick-GGX geometry term, one side
float geometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float denom = NdotV * (1.0 - k) + k;
    return NdotV / max(denom, 0.0000001);
}

// Smith geometry term, both view and light sides
float geometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggxV = geometrySchlickGGX(NdotV, roughness);
    float ggxL = geometrySchlickGGX(NdotL, roughness);
    return ggxV * ggxL;
}

float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (float3(1.0, 1.0, 1.0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Cook-Torrance specular + Lambertian diffuse, energy-conserving split via Fresnel/metallic
float3 shadeLight(float3 N, float3 V, float3 L, float3 radiance, float3 albedo, float metallic, float roughness)
{
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    float3 H = normalize(V + L);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    float3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
    float3 specular = numerator / denominator;

    float3 kD = (float3(1.0, 1.0, 1.0) - F) * (1.0 - metallic);

    return (kD * albedo / kPi + specular) * radiance * NdotL;
}

// Inverse-square falloff, smoothly windowed to zero at range - avoids the classic hard cutoff pop
float attenuateRange(float dist, float range)
{
    float rangeFalloff = clamp(1.0 - pow(dist / max(range, 0.0001), 4.0), 0.0, 1.0);
    rangeFalloff = rangeFalloff * rangeFalloff;
    return rangeFalloff / max(dist * dist, 0.0001);
}

// Cluster light-count -> color ramp for the clustered-lighting debug view (blue = empty, red = hot).
// kHeatmapMaxLights is the count that reads as fully red; tune to whatever "too many lights in one
// cluster" means for the scene you're profiling
float3 clusterHeatmapColor(uint lightCount)
{
    const float kHeatmapMaxLights = 8.0;
    float t = clamp(float(lightCount) / kHeatmapMaxLights, 0.0, 1.0);

    float3 c0 = float3(0.0, 0.0, 1.0);
    float3 c1 = float3(0.0, 1.0, 0.0);
    float3 c2 = float3(1.0, 1.0, 0.0);
    float3 c3 = float3(1.0, 0.0, 0.0);

    if (t < 0.333) return lerp(c0, c1, t / 0.333);
    if (t < 0.667) return lerp(c1, c2, (t - 0.333) / 0.334);
    return lerp(c2, c3, (t - 0.667) / 0.333);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    // Clustered PointLight/Spotlight lookup, via the interpolated view-space Z (log-slice inverse) and this
    // fragment's screen-space tile - must match the linearization ClusterLightingPass's compute shaders use
    // (i + j*dimX + k*dimX*dimY). Computed up front since both the heatmap debug view and normal shading need it
    float dist = max(-input.viewSpaceZ, gClusterParams.screenSizeNearFar.z);
    float near = gClusterParams.screenSizeNearFar.z;
    float far = gClusterParams.screenSizeNearFar.w;
    uint dimX = gClusterParams.clusterDims.x;
    uint dimY = gClusterParams.clusterDims.y;
    uint dimZ = gClusterParams.clusterDims.z;

    uint zSlice = uint(clamp(log(dist / near) * float(dimZ) / log(far / near), 0.0, float(dimZ - 1)));

    float2 tileSize = gClusterParams.screenSizeNearFar.xy / float2(dimX, dimY);
    uint2 tile = uint2(clamp(input.position.xy / tileSize, float2(0.0, 0.0), float2(dimX - 1, dimY - 1)));

    uint clusterIndex = tile.x + tile.y * dimX + zSlice * dimX * dimY;
    uint clusterCount = dimX * dimY * dimZ;

    uint lightCountInCluster = 0;
    if (clusterIndex < clusterCount)
    {
        lightCountInCluster = gClusterLightGrid[clusterIndex];
    }

    // Debug view: skip shading entirely and show the raw per-cluster light count instead
    if (gPush.pbrParams.z > 0.5)
    {
        return float4(clusterHeatmapColor(lightCountInCluster), 1.0);
    }

    float3 albedo = gAlbedoSampler.Sample(input.uv).rgb * gPush.color.rgb;
    float metallic = gPush.pbrParams.x;
    float roughness = clamp(gPush.pbrParams.y, 0.045, 1.0);

    // Gram-Schmidt re-orthogonalize the interpolated tangent against the interpolated normal, then derive
    // the bitangent from the vertex-supplied handedness sign - standard TBN reconstruction per-pixel
    float3 N = normalize(input.worldNormal);
    float3 T = normalize(input.worldTangent - N * dot(N, input.worldTangent));
    float3 B = cross(N, T) * input.tangentSign;

    float3 tangentNormal = gNormalSampler.Sample(input.uv).rgb * 2.0 - 1.0;
    N = normalize(T * tangentNormal.x + B * tangentNormal.y + N * tangentNormal.z);

    float3 V = normalize(gCamera.position - input.worldPosition);

    float3 color = float3(0.0, 0.0, 0.0);

    // Global (unclustered) lights
    for (uint di = 0; di < gCamera.directionalLightCountPad.x; ++di)
    {
        DirectionalLightGpu light = gCamera.directionalLights[di];
        float3 L = normalize(-light.direction.xyz);
        float3 radiance = light.colorIntensity.rgb * light.colorIntensity.a;
        color += shadeLight(N, V, L, radiance, albedo, metallic, roughness);
    }

    if (clusterIndex < clusterCount)
    {
        uint indexBase = clusterIndex * gClusterParams.clusterDims.w;

        for (uint li = 0; li < lightCountInCluster; ++li)
        {
            GpuLight light = gLights[gLightIndexList[indexBase + li]];

            float3 toLight = light.worldPosRange.xyz - input.worldPosition;
            float lightDist = length(toLight);
            float3 L = toLight / max(lightDist, 0.0001);

            float atten = attenuateRange(lightDist, light.worldPosRange.w);
            if (light.viewPosType.w > 0.5)
            {
                // Spotlight: cone smoothstep between inner/outer angle on top of the range falloff
                float cosAngle = dot(-L, normalize(light.directionPad.xyz));
                atten *= smoothstep(light.coneAngles.x, light.coneAngles.y, cosAngle);
            }

            float3 radiance = light.colorIntensity.rgb * light.colorIntensity.a * atten;
            color += shadeLight(N, V, L, radiance, albedo, metallic, roughness);
        }
    }

    // Flat ambient term - AmbientLight has no position/direction to shade directionally
    color += gCamera.ambientColorIntensity.rgb * gCamera.ambientColorIntensity.a * albedo * (1.0 - metallic);

    // No tonemapping existed anywhere in this engine before - raw PBR output clips to white the instant a
    // light's radiance exceeds 1.0, instead of rolling off. Reinhard compresses HDR into displayable range,
    // then gamma 2.2 undoes the display's sRGB decode so mid-tones don't look washed out
    color = color / (color + float3(1.0, 1.0, 1.0));
    color = pow(color, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));

    return float4(color, gPush.color.a);
}
