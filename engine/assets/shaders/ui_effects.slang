#define BLUR_SIZE 7
#define BLUR_NUM_WEIGHTS 4
#define MAX_NUM_STOPS 16
#define PI 3.14159265

// Shared 96-byte push constant:
//   vsFullscreen: v0.xy = uv offset
//                 v0.zw = uv scale
//   fsPassthrough: v1.x = blend factor
//   fsBlur: v1.xy = texel offset
//           v2.xy/zw = texcoord min/max
//           v3 = weights[4]
//   fsDropShadow: v1 = shadow color
//                 v2.xy/zw = texcoord min/max
//   fsColorMatrix: v2..v5 = color matrix rows
struct EffectsPush {
    float4 v0;
    float4 v1;
    float4 v2;
    float4 v3;
    float4 v4;
    float4 v5;
};

[[vk::push_constant]]
EffectsPush gPush;

[[vk::binding(0,0)]]
Sampler2D gTexture;

[[vk::binding(0,1)]]
Sampler2D gMaskTexture;

struct FSInput {
    float4 position : SV_Position;

    [[vk::location(0)]]
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
FSInput vsFullscreen(uint vertex_id : SV_VertexID) {
    FSInput output;
    float2 uv = float2((vertex_id << 1) & 2, vertex_id & 2);
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    output.uv = uv * gPush.v0.zw + gPush.v0.xy;
    return output;
}

[shader("fragment")]
float4 fsPassthrough(FSInput input) : SV_Target0 {
    return gTexture.Sample(input.uv) * gPush.v1.x;
}

[shader("fragment")]
float4 fsColorMatrix(FSInput input) : SV_Target0 {
    float4x4 m = float4x4(gPush.v2, gPush.v3, gPush.v4, gPush.v5);
    float4 tex_color = gTexture.Sample(input.uv);
    float3 transformed = mul(m, tex_color).rgb;
    return float4(transformed, tex_color.a);
}

[shader("fragment")]
float4 fsBlendMask(FSInput input) : SV_Target0 {
    float4 tex_color = gTexture.Sample(input.uv);
    float mask_alpha = gMaskTexture.Sample(input.uv).a;
    return tex_color * mask_alpha;
}

[shader("fragment")]
float4 fsBlur(FSInput input) : SV_Target0 {
    float2 texel_offset = gPush.v1.xy;
    float2 uv_min = gPush.v2.xy;
    float2 uv_max = gPush.v2.zw;

    float4 color = float4(0.0);
    [unroll]
    for (int i = 0; i < BLUR_SIZE; i++)
    {
        float2 uv = input.uv - float(i - BLUR_NUM_WEIGHTS + 1) * texel_offset;
        float2 in_region = step(uv_min, uv) * step(uv, uv_max);
        color += gTexture.Sample(uv) * in_region.x * in_region.y * gPush.v3[abs(i - BLUR_NUM_WEIGHTS + 1)];
    }
    return color;
}

[shader("fragment")]
float4 fsDropShadow(FSInput input) : SV_Target0 {
    float2 uv_min = gPush.v2.xy;
    float2 uv_max = gPush.v2.zw;
    float2 in_region = step(uv_min, input.uv) * step(input.uv, uv_max);
    return gTexture.Sample(input.uv).a * in_region.x * in_region.y * gPush.v1;
}

struct VertexUserData
{
    float4x4 transform;
    float2 translate;
};

[[vk::binding(1,0)]]
ConstantBuffer<VertexUserData> gVertexData;

struct GradientData {
    int func; // 0 linear, 1 radial, 2 conic, 3-5 repeating variants
    int num_stops;
    float2 p;
    float2 v;
    float2 _padding;
    float4 stop_colors[MAX_NUM_STOPS];
    float4 stop_positions[MAX_NUM_STOPS / 4];
};

[[vk::binding(2,1)]]
ConstantBuffer<GradientData> gGradient;

struct GeometryVSInput {
    [[vk::location(0)]]
    float2 position : POSITION;

    [[vk::location(1)]]
    float4 color : COLOR;

    [[vk::location(2)]]
    float2 uv : TEXCOORD0;
};

struct GeometryFSInput {
    float4 position : SV_Position;

    [[vk::location(0)]]
    float4 color : COLOR;

    [[vk::location(1)]]
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
GeometryFSInput vsGeometry(GeometryVSInput input) {
    GeometryFSInput output;
    float2 translated = input.position + gVertexData.translate;
    output.position = mul(gVertexData.transform, float4(translated, 0.0, 1.0));
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

float stopPosition(int i) {
    return gGradient.stop_positions[i / 4][i % 4];
}

float4 mixStopColors(float t) {
    float4 color = gGradient.stop_colors[0];
    for (int i = 1; i < gGradient.num_stops; i++)
        color = lerp(color, gGradient.stop_colors[i], smoothstep(stopPosition(i - 1), stopPosition(i), t));
    return color;
}

[shader("fragment")]
float4 fsGradient(GeometryFSInput input) : SV_Target0 {
    int func = gGradient.func;
    float t = 0.0;

    if (func == 0 || func == 3) {
        // linear
        float dist_square = dot(gGradient.v, gGradient.v);
        float2 V = input.uv - gGradient.p;
        t = dot(gGradient.v, V) / dist_square;
    } else if (func == 1 || func == 4) {
        // radial
        float2 V = input.uv - gGradient.p;
        t = length(gGradient.v * V);
    } else if (func == 2 || func == 5) {
        // conic
        float2x2 R = float2x2(gGradient.v.x, -gGradient.v.y, gGradient.v.y, gGradient.v.x);
        float2 V = mul(R, input.uv - gGradient.p);
        t = 0.5 + atan2(-V.x, V.y) / (2.0 * PI);
    }

    if (func >= 3) {
        // repeating
        float t0 = stopPosition(0);
        float t1 = stopPosition(gGradient.num_stops - 1);
        t = t0 + fmod(t - t0, t1 - t0);
    }

    return input.color * mixStopColors(t);
}
