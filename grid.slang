struct CameraUBO
{
    float4x4 view;
    float4x4 projection;
    float4x4 viewProjection;

    float3 cameraPosition;
    float time;
};

[[vk::binding(0,0)]]
ConstantBuffer<CameraUBO> gCamera;

struct PushConstants
{
    float4x4 model;
};

[[vk::push_constant]]
PushConstants gPush;

struct VSInput
{
    [[vk::location(0)]]
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_Position;

    [[vk::location(0)]]
    float3 worldPos : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    VSOutput output;

    float4 worldPos =
        mul(
            gPush.model,
            float4(input.position, 1.0));

    output.worldPos = worldPos.xyz;

    output.position =
        mul(
            gCamera.viewProjection,
            worldPos);

    return output;
}

float GridLine(float2 coord, float scale)
{
    float2 p = coord / scale;

    float2 grid =
        abs(frac(p - 0.5) - 0.5) /
        max(fwidth(p), 1e-6);

    float line =
        min(grid.x, grid.y);

    return 1.0 - saturate(line);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    //
    // Z-UP
    //
    float2 coord = input.worldPos.xy;

    //
    // Distance fade
    //
    float dist =
        distance(
            gCamera.cameraPosition,
            input.worldPos);

    float minorFade =
        saturate(
            1.0 -
            dist / 50.0);

    float majorFade =
        saturate(
            1.0 -
            dist / 500.0);

    //
    // Grid
    //
    float minorGrid =
        GridLine(coord, 1.0);

    float majorGrid =
        GridLine(coord, 10.0);

    //
    // Base colors
    //
    float3 color =
        float3(
            0.08,
            0.08,
            0.08);

    color +=
        minorGrid *
        minorFade *
        float3(
            0.15,
            0.15,
            0.15);

    color +=
        majorGrid *
        majorFade *
        float3(
            0.35,
            0.35,
            0.35);

    //
    // Axis highlighting
    //
    float axisWidth = 0.05;

    float xAxis =
        1.0 -
        smoothstep(
            axisWidth,
            axisWidth + fwidth(coord.y),
            abs(coord.y));

    float yAxis =
        1.0 -
        smoothstep(
            axisWidth,
            axisWidth + fwidth(coord.x),
            abs(coord.x));

    //
    // Blender / Unreal style:
    // X = red
    // Y = green
    // Z = up
    //
    color =
        lerp(
            color,
            float3(1.0, 0.1, 0.1),
            xAxis);

    color =
        lerp(
            color,
            float3(0.1, 1.0, 0.1),
            yAxis);

    //
    // Fade whole grid at extreme distance
    //
    float alpha =
        saturate(
            1.0 -
            dist / 1000.0);

    return float4(color, alpha);
}