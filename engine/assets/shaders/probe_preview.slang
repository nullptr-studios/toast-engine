struct ProbePreviewParams
{
    float4x4 viewProjection;

    // xyz centre w radius
    float4 centerRadius;

    float4 cameraPosition;
};

[[vk::push_constant]]
ProbePreviewParams gParams;

[[vk::binding(0,0)]]
SamplerCube gProbeCube;

struct VSOutput
{
    float4 position : SV_Position;

    [[vk::location(0)]]
    float3 worldPosition : TEXCOORD0;

    [[vk::location(1)]]
    float3 normal : NORMAL;
};

static const uint kRings = 24;
static const uint kSegments = 48;

float3 spherePoint(uint ring, uint segment)
{
    float v = float(ring) / float(kRings);
    float u = float(segment) / float(kSegments);

    float phi = v * 3.14159265359;
    float theta = u * 2.0 * 3.14159265359;

    float sinPhi = sin(phi);
    return float3(sinPhi * cos(theta), sinPhi * sin(theta), cos(phi));
}

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    uint quad = vertexId / 6u;
    uint corner = vertexId % 6u;

    uint ring = quad / kSegments;
    uint segment = quad % kSegments;

    uint2 offsets[6] = {
        uint2(0u, 0u), uint2(0u, 1u), uint2(1u, 1u),
        uint2(0u, 0u), uint2(1u, 1u), uint2(1u, 0u)
    };
    uint2 offset = offsets[corner];

    float3 normal = spherePoint(ring + offset.x, segment + offset.y);
    float3 world = gParams.centerRadius.xyz + normal * gParams.centerRadius.w;

    VSOutput output;
    output.position = mul(gParams.viewProjection, float4(world, 1.0));
    output.worldPosition = world;
    output.normal = normal;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    float3 N = normalize(input.normal);
    float3 V = normalize(gParams.cameraPosition.xyz - input.worldPosition);

    return float4(gProbeCube.SampleLevel(reflect(-V, N), 0.0).rgb, 1.0);
}
