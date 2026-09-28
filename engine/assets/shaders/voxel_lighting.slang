import fullscreen;
import voxel_gbuffer;
import lighting;

// Mirrors VoxelPass::LightingPushConstants
struct LightingPushConstants
{
    // Must be jittered since depth was rasterized with it
    float4x4 inverseViewProjection;
};

[[vk::push_constant]]
ConstantBuffer<LightingPushConstants> gPush;

[[vk::binding(0, 1)]]
Sampler2D gAlbedo;

[[vk::binding(1, 1)]]
Sampler2D gNormalRoughness;

[[vk::binding(2, 1)]]
Sampler2D gMaterial;

[[vk::binding(3, 1)]]
Sampler2D gDepth;

// Unrounded so shadow bias still sees the flat face
[[vk::binding(4, 1)]]
Sampler2D gFaceNormal;

typedef FullscreenVSOutput VSOutput;

struct FSOutput
{
    float4 color : SV_Target0;
    float4 indirectDiffuse : SV_Target1;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    return fullscreenTriangle(vertexId);
}

[shader("fragment")]
FSOutput fragmentMain(VSOutput input)
{
    const int3 texel = int3(int2(input.position.xy), 0);
    const float4 albedo = gAlbedo.Load(texel);
    if (!isVoxelCoverage(albedo.a))
    {
        discard;
    }

    FSOutput output;
    output.indirectDiffuse = float4(0.0, 0.0, 0.0, 1.0);
    if (!isLitVoxel(albedo.a))
    {
        output.color = float4(albedo.rgb, 1.0);
        return output;
    }

    const float depth = gDepth.Load(texel).r;
    const float4 world = mul(gPush.inverseViewProjection, float4(input.uv * 2.0 - 1.0, depth, 1.0));
    const float3 position = world.xyz / world.w;

    const float3 normal = normalize(gNormalRoughness.Load(texel).xyz);
    const float3 faceNormal = normalize(gFaceNormal.Load(texel).xyz);
    const float4 material = gMaterial.Load(texel);

    // The near plane clip can put the hit on the camera
    const float3 toCamera = gCamera.position - position;
    const float3 view = dot(toCamera, toCamera) > 1e-10 ? normalize(toCamera) : normal;

    Surface surface;
    surface.worldPosition = position;
    surface.normal = normal;
    surface.geometricNormal = faceNormal;
    surface.viewDirection = view;
    surface.albedo = albedo.rgb;
    surface.metallic = material.r;
    surface.reflectivity = material.g;
    surface.roughness = clamp(material.b, 0.045, 1.0);
    surface.occlusion = 1.0;
    surface.viewSpaceZ = mul(gCamera.view, float4(position, 1.0)).z;
    surface.pixel = input.position.xy;

    const ShadingResult lit = shadeSurfaceLighting(surface);
    output.indirectDiffuse = float4(lit.indirectDiffuse, 1.0);
    output.color = float4(lit.isDebugView ? lit.color : lit.color + albedo.rgb * decodeEmissive(material.a), 1.0);
    return output;
}
