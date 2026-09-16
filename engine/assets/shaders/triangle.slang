struct VS_Output {
    float4 position : SV_Position;
    float3 color : COLOR;
};

[shader("vertex")]
VS_Output vertexMain(in uint vertexID : SV_VertexID) {
    float3 positions[] = {
        float3(0.0, 0.5, 0.0),
        float3(0.5, -0.5, 0.0),
        float3(-0.5, -0.5, 0.0)
    };
    
    float3 colors[] = {
        float3(1.0, 0.0, 0.0),
        float3(0.0, 0.0, 1.0),
        float3(0.0, 1.0, 0.0)
    };
    
    VS_Output output;
    output.position = float4(positions[vertexID], 1.0);
    output.color = colors[vertexID];
    return output;
}

[shader("fragment")]
float4 fragmentMain(VS_Output input) : SV_Target {
    return float4(input.color, 1.0);
}
