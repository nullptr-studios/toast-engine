// --------------------------------------------------------------------------
// DATA STRUCTURES
// --------------------------------------------------------------------------
struct FrameUniform
{
    float2 iResolution; // Sits at offset 0
    float  iTime;       // Sits at offset 8
};

[[vk::binding(0, 0)]]
ConstantBuffer<FrameUniform> frameData;

struct VS_Output
{
    float4 position : SV_Position;
};

// --------------------------------------------------------------------------
// HARDCODED CONSTANTS & GLOBALS (No Uniforms)
// --------------------------------------------------------------------------
static const float  REPEAT = 100.0;

// Internal global state used by the procedural generator
static float gTime = 0.0;

// --------------------------------------------------------------------------
// SHADER MATHEMATICS
// --------------------------------------------------------------------------
// Native Slang float2x2 matrix generation
float2x2 rot(float a) {
    float c = cos(a), s = sin(a);
    return float2x2(c, s, -s, c);
}

// GLSL custom mod behavior emulation for repetition
float3 glsl_mod(float3 x, float y) { 
    return x - y * floor(x / y); 
}

// Optimized signed distance bounding box
float sdBox(float3 p, float3 b)
{
    float3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float box(float3 pos, float scale) {
    pos *= scale;
    float base = sdBox(pos, float3(.4, .4, .1)) / 1.5;
    pos.xy = mul(pos.xy, rot(.75));
    float result = -base;
    return result;
}

float box_set(float3 pos) {
    float3 pos_origin = pos;
    
    pos = pos_origin;
    pos.y += sin(gTime * 0.4) * 2.5;
    pos.xy = mul(pos.xy, rot(.8));
    float box1 = box(pos, 2. - abs(sin(gTime * 0.4)) * 1.5);
    
    pos = pos_origin;
    pos.y -= sin(gTime * 0.4) * 2.5;
    pos.xy = mul(pos.xy, rot(.8));
    float box2 = box(pos, 2. - abs(sin(gTime * 0.4)) * 1.5);
    
    pos = pos_origin;
    pos.x += sin(gTime * 0.4) * 2.5;
    pos.xy = mul(pos.xy, rot(.8));
    float box3 = box(pos, 2. - abs(sin(gTime * 0.4)) * 1.5);    
    
    pos = pos_origin;
    pos.x -= sin(gTime * 0.4) * 2.5;
    pos.xy = mul(pos.xy, rot(.8));
    float box4 = box(pos, 2. - abs(sin(gTime * 0.4)) * 1.5);    
    
    pos = pos_origin;
    pos.xy = mul(pos.xy, rot(.8));
    float box5 = box(pos, .5) * 6.;    
    
    pos = pos_origin;
    float box6 = box(pos, .5) * 6.;    
    
    float result = max(max(max(max(max(box1, box2), box3), box4), box5), box6);
    return result;
}

float map(float3 pos) {
    return box_set(pos);
}

// --------------------------------------------------------------------------
// MAIN ENTRY POINTS
// --------------------------------------------------------------------------

[shader("vertex")]
VS_Output vertexMain(uint vertexID : SV_VertexID)
{
    static const float2 positions[6] = {
        float2( 1.0,  1.0),
        float2(-1.0,  1.0),
        float2( 1.0, -1.0),
        
        float2( 1.0, -1.0),
        float2(-1.0,  1.0),
        float2(-1.0, -1.0) 
    };

    VS_Output output;
    output.position = float4(positions[vertexID], 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VS_Output input) : SV_Target
{   
    float floatTime = frameData.iTime;
    float2 res = frameData.iResolution;

    float2 fragCoord = input.position.xy;
    fragCoord.y = res.y - fragCoord.y; // Use dynamic resolution

    float2 p = (fragCoord.xy * 2. - res.xy) / min(res.x, res.y);
    float3 ro = float3(0., -0.2, floatTime * 4.);
    float3 ray = normalize(float3(p, 1.5));
    
    ray.xy = mul(ray.xy, rot(sin(floatTime * .03) * 5.));
    ray.yz = mul(ray.yz, rot(sin(floatTime * .05) * .2));
    
    float t = 0.1;
    float3 col = float3(0.);
    float ac = 0.0;

    for (int i = 0; i < 99; i++) {
        float3 pos = ro + ray * t;
        pos = glsl_mod(pos - 2., 4.) - 2.;
        gTime = floatTime - float(i) * 0.01; // Global synchronization tracking
        
        float d = map(pos);
        d = max(abs(d), 0.01);
        ac += exp(-d * 23.0);
        t += d * 0.55;
    }

    col = float3(ac * 0.02);
    col += float3(0., 0.2 * abs(sin(floatTime)), 0.5 + sin(floatTime) * 0.2);

    return float4(col, 1.0 - t * (0.02 + 0.02 * sin(floatTime)));
}