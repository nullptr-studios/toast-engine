// No resources so every GI pass can import it
import depth_convention;

static const uint kGiEmpty = 0xFFFFFFFFu;

uint giHash(uint key)
{
    key ^= key >> 16;
    key *= 0x7feb352du;
    key ^= key >> 15;
    key *= 0x846ca68bu;
    key ^= key >> 16;
    return key;
}

float giRandom(inout uint state)
{
    state = giHash(state + 0x9e3779b9u);
    return float(state >> 8) / 16777216.0;
}

float2 giOctEncode(float3 n)
{
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    float2 p = n.xy;
    if (n.z < 0.0)
    {
        p = (1.0 - abs(n.yx)) * float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return p * 0.5 + 0.5;
}

float3 giOctDecode(float2 uv)
{
    const float2 f = uv * 2.0 - 1.0;
    float3 n = float3(f, 1.0 - abs(f.x) - abs(f.y));
    const float fold = saturate(-n.z);
    n.x += n.x >= 0.0 ? -fold : fold;
    n.y += n.y >= 0.0 ? -fold : fold;
    return normalize(n);
}
