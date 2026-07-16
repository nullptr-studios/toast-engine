// triangle.slang - Simple colored triangle shader (Vulkan SPIR-V)
// Generates a triangle with per-vertex colors using gl_VertexIndex

struct VS_Output {
    float4 position : SV_Position;
    float3 color : COLOR;
};

// Vertex shader entry point
[shader("vertex")]
VS_Output vertexMain(in uint vertexID : SV_VertexID) {
    // Define hardcoded triangle vertices in NDC space (counter-clockwise winding for front face)
    float3 positions[] = {
        float3(0.0, 0.5, 0.0),      // Top
        float3(0.5, -0.5, 0.0),     // Bottom-right
        float3(-0.5, -0.5, 0.0)     // Bottom-left
    };
    
    float3 colors[] = {
        float3(1.0, 0.0, 0.0),      // Red
        float3(0.0, 0.0, 1.0),      // Blue
        float3(0.0, 1.0, 0.0)       // Green
    };
    
    VS_Output output;
    output.position = float4(positions[vertexID], 1.0);
    output.color = colors[vertexID];
    return output;
}

// Fragment shader entry point
[shader("fragment")]
float4 fragmentMain(VS_Output input) : SV_Target {
    return float4(input.color, 1.0);
}




