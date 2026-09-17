import fullscreen;

struct TracedShadowParams
{
    float4x4 inverseViewProjection;
    float4 lightDirection;
    // x 0 visibility 1 applied y normal bias z max trace distance
    float4 tuning;
};

[[vk::push_constant]]
ConstantBuffer<TracedShadowParams> gParams;

[[vk::binding(0,0)]]
Sampler2D gSceneColor;

[[vk::binding(1,0)]]
Sampler2D gDepth;

[[vk::binding(2,0)]]
Sampler2D gNormalRoughness;

[[vk::binding(3,0)]]
RaytracingAccelerationStructure gScene;

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

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    float3 sceneColor = gSceneColor.Sample(input.uv).rgb;

    bool applied = gParams.tuning.x > 0.5;

    float4 normalRoughness = gNormalRoughness.Sample(input.uv);
    float depth = gDepth.Sample(input.uv).r;

    if (dot(normalRoughness.xyz, normalRoughness.xyz) < 1e-4 || depth >= 1.0)
    {
        return float4(applied ? sceneColor : float3(1.0, 1.0, 1.0), 1.0);
    }

    float3 N = normalize(normalRoughness.xyz);
    float3 worldPosition = worldPositionFromDepth(input.uv, depth);

    float3 L = normalize(-gParams.lightDirection.xyz);

    float NdotL = dot(N, L);
    if (NdotL <= 0.0)
    {
        return float4(float3(0.0, 0.0, 0.0), 1.0);
    }

    RayDesc ray;
    ray.Origin = worldPosition + N * gParams.tuning.y;
    ray.Direction = L;
    ray.TMin = 0.0;
    ray.TMax = gParams.tuning.z;

    RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(gScene, RAY_FLAG_NONE, 0xFF, ray);

    // ACCEPT_FIRST_HIT_AND_END_SEARCH so no Proceed() loop is needed
    query.Proceed();

    float visibility = query.CommittedStatus() == COMMITTED_NOTHING ? 1.0 : 0.0;

    if (!applied)
    {
        return float4(visibility, visibility, visibility, 1.0);
    }
    return float4(sceneColor * visibility, 1.0);
}
