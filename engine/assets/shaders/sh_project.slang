struct ShProjectParams
{
    // x cube face size y destination probe index
    uint4 config;
};

[[vk::push_constant]]
ConstantBuffer<ShProjectParams> gParams;

[[vk::binding(0, 0)]]
SamplerCube gSource;

// Four float4 per probe L0 L1y L1z L1x
[[vk::binding(1, 0)]]
RWStructuredBuffer<float4> gCoefficients;

// Must match renderer::cubeFaceBasis()
float3 directionForTexel(uint face, float2 uv)
{
    float2 st = uv * 2.0 - 1.0;

    switch (face)
    {
    case 0: return normalize(float3(1.0, -st.y, -st.x));   // +X
    case 1: return normalize(float3(-1.0, -st.y, st.x));   // -X
    case 2: return normalize(float3(st.x, 1.0, st.y));     // +Y
    case 3: return normalize(float3(st.x, -1.0, -st.y));   // -Y
    case 4: return normalize(float3(st.x, -st.y, 1.0));    // +Z
    default: return normalize(float3(-st.x, -st.y, -1.0)); // -Z
    }
}

float texelSolidAngle(float2 st, float texelSize)
{
    float x0 = st.x - texelSize;
    float y0 = st.y - texelSize;
    float x1 = st.x + texelSize;
    float y1 = st.y + texelSize;

    float a = atan2(x0 * y0, sqrt(x0 * x0 + y0 * y0 + 1.0));
    float b = atan2(x0 * y1, sqrt(x0 * x0 + y1 * y1 + 1.0));
    float c = atan2(x1 * y0, sqrt(x1 * x1 + y0 * y0 + 1.0));
    float d = atan2(x1 * y1, sqrt(x1 * x1 + y1 * y1 + 1.0));
    return a - b - c + d;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 threadId : SV_DispatchThreadID)
{
    uint faceSize = gParams.config.x;
    uint probeIndex = gParams.config.y;

    float3 l0 = float3(0.0, 0.0, 0.0);
    float3 l1y = float3(0.0, 0.0, 0.0);
    float3 l1z = float3(0.0, 0.0, 0.0);
    float3 l1x = float3(0.0, 0.0, 0.0);
    float totalWeight = 0.0;

    float texelSize = 1.0 / float(faceSize);

    for (uint face = 0; face < 6; ++face)
    {
        for (uint y = 0; y < faceSize; ++y)
        {
            for (uint x = 0; x < faceSize; ++x)
            {
                float2 uv = (float2(x, y) + 0.5) * texelSize;
                float3 direction = directionForTexel(face, uv);

                float2 st = uv * 2.0 - 1.0;
                float weight = texelSolidAngle(st, texelSize);

                float3 radiance = gSource.SampleLevel(direction, 0.0).rgb;

                // Cosine convolution happens at lookup
                l0 += radiance * 0.282095 * weight;
                l1y += radiance * 0.488603 * direction.y * weight;
                l1z += radiance * 0.488603 * direction.z * weight;
                l1x += radiance * 0.488603 * direction.x * weight;

                totalWeight += weight;
            }
        }
    }

    float normalization = totalWeight > 0.0 ? (4.0 * 3.14159265 / totalWeight) : 0.0;

    uint base = probeIndex * 4;
    gCoefficients[base + 0] = float4(l0 * normalization, 0.0);
    gCoefficients[base + 1] = float4(l1y * normalization, 0.0);
    gCoefficients[base + 2] = float4(l1z * normalization, 0.0);
    gCoefficients[base + 3] = float4(l1x * normalization, 0.0);
}
