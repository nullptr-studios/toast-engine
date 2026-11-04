// GGX visible normal sampling shared by SSR and traced specular

static const float kGgxPi = 3.14159265;

float ggxD(float NdotH, float alpha)
{
    const float a2 = alpha * alpha;
    const float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(kGgxPi * d * d, 1e-8);
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
    const float phi = 2.0 * kGgxPi * u.y;
    const float p1 = r * cos(phi);
    const float s = 0.5 * (1.0 + stretched.z);
    const float p2 = (1.0 - s) * sqrt(max(1.0 - p1 * p1, 0.0)) + s * r * sin(phi);
    const float3 h = p1 * t1 + p2 * t2 + sqrt(max(1.0 - p1 * p1 - p2 * p2, 0.0)) * stretched;
    return normalize(float3(alpha * h.x, alpha * h.y, max(h.z, 0.0)));
}

/// Any 4x4 texel window holds every stratum once
static const uint kLobeStrata = 16u;
static const uint kLobeBayer[16] = {0u, 8u, 2u, 10u, 12u, 4u, 14u, 6u, 3u, 11u, 1u, 9u, 15u, 7u, 13u, 5u};

uint lobeStratumIndex(int2 cell)
{
    return kLobeBayer[(uint(cell.y) & 3u) * 4u + (uint(cell.x) & 3u)];
}

/// Hammersley point of a stratum
float2 lobeStratumSample(uint index)
{
    const uint reversed = ((index & 1u) << 3) | ((index & 2u) << 1) | ((index & 4u) >> 1) | ((index & 8u) >> 3);
    return (float2(float(index), float(reversed)) + 0.5) / float(kLobeStrata);
}

float2 lobeStratum(int2 texel)
{
    return lobeStratumSample(lobeStratumIndex(texel));
}

/// Every stratum of the 4x4 tile weighs the same over the 5x5 tent
float lobeStratumWeight(int2 offset)
{
    return (abs(offset.x) == 2 ? 0.5 : 1.0) * (abs(offset.y) == 2 ? 0.5 : 1.0);
}

/// Of a direction sampled through sampleVisibleNormal
float visibleNormalPdf(float NdotV, float NdotH, float alpha)
{
    return smithG1(NdotV, alpha) * ggxD(NdotH, alpha) / max(4.0 * NdotV, 1e-8);
}
