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

/// Of a direction sampled through sampleVisibleNormal
float visibleNormalPdf(float NdotV, float NdotH, float alpha)
{
    return smithG1(NdotV, alpha) * ggxD(NdotH, alpha) / max(4.0 * NdotV, 1e-8);
}
