import fullscreen;
import gi_clip_common;

[[vk::binding(0, 0)]]
Sampler2D gSource;

[[vk::binding(1, 0)]]
StructuredBuffer<GiClipFrame> gClipFrame;

[[vk::binding(2, 0)]]
Sampler3D gMaterial;

[[vk::binding(3, 0)]]
Sampler3D gEmission;

[[vk::binding(4, 0)]]
Sampler3D gRadiance[kGiClipFaces];

struct GiClipDebugPush
{
    uint level;
    /// Mirrors renderer::GiSettings clip_debug_mode
    uint mode;
};

[[vk::push_constant]]
GiClipDebugPush gPush;

static const uint kGiClipDebugEmission = 1u;
static const uint kGiClipDebugOpacity = 2u;
static const uint kGiClipDebugRadiance = 3u;

typedef FullscreenVSOutput VSOutput;

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    return fullscreenTriangle(vertexId);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    const float4 scene = gSource.Load(int3(int2(input.position.xy), 0));
    const GiClipFrame frame = gClipFrame[0];
    const uint level = min(gPush.level, frame.size.y - 1u);
    const int resolution = int(frame.size.x);
    const float cellSize = frame.levels[level].w;

    // Depth 0.5 is in front of the camera with or without reversed Z
    const float4 ahead = mul(frame.inverseViewProjection, float4(input.uv * 2.0 - 1.0, 0.5, 1.0));
    float3 direction = normalize(ahead.xyz / ahead.w - frame.camera.xyz);
    direction = select(abs(direction) > 1e-6, direction, float3(1e-6, 1e-6, 1e-6));
    const float3 origin = frame.camera.xyz / cellSize;

    const float3 boxMin = frame.levels[level].xyz;
    const float3 boxMax = boxMin + float(resolution);
    const float3 inverse = 1.0 / direction;
    const float3 t0 = (boxMin - origin) * inverse;
    const float3 t1 = (boxMax - origin) * inverse;
    const float3 near = min(t0, t1);
    const float3 far = max(t0, t1);
    const float tEnter = max(max(near.x, max(near.y, near.z)), 0.0);
    const float tExit = min(far.x, min(far.y, far.z));
    if (tEnter >= tExit)
    {
        return scene;
    }

    const int3 low = int3(boxMin);
    int3 cell = clamp(int3(floor(origin + direction * (tEnter + 1e-4))), low, low + resolution - 1);
    const int3 stepSign = int3(sign(direction));
    const float3 cellT = abs(inverse);
    float3 nextT = (float3(cell) + max(float3(stepSign), 0.0) - origin) * inverse;
    int axis = near.x > near.y ? (near.x > near.z ? 0 : 2) : (near.y > near.z ? 1 : 2);

    for (int steps = 0; steps < 3 * resolution; ++steps)
    {
        const int4 texel = int4(giClipWrap(cell, resolution), 0);
        const float4 material = gMaterial.Load(texel);
        if (material.a > 0.0)
        {
            float3 colour = material.rgb;
            if (gPush.mode == kGiClipDebugEmission)
            {
                colour = gEmission.Load(texel).rgb;
            }
            else if (gPush.mode == kGiClipDebugOpacity)
            {
                colour = float3(1.0, 1.0, 1.0);
            }
            if (gPush.mode == kGiClipDebugRadiance)
            {
                const uint face = uint(axis) * 2u + (stepSign[axis] > 0 ? 1u : 0u);
                return float4(gRadiance[face].Load(texel).rgb, scene.a);
            }
            const float shade = axis == 0 ? 1.0 : (axis == 1 ? 0.8 : 0.6);
            return float4(colour * shade, scene.a);
        }
        axis = nextT.x < nextT.y ? (nextT.x < nextT.z ? 0 : 2) : (nextT.y < nextT.z ? 1 : 2);
        cell[axis] += stepSign[axis];
        nextT[axis] += cellT[axis];
        if (cell[axis] < low[axis] || cell[axis] >= low[axis] + resolution)
        {
            break;
        }
    }
    return scene;
}
