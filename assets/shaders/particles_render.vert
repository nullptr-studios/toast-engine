#version 430 core

layout(location = 0) in vec2 a_quad; // unit quad (-0.5..0.5)

struct Particle {
    vec4 pos;      // xyz = position; w = currentSize
    vec4 vel;      // xyz = velocity; w = rotation
    vec4 color;    // start color rgba
    vec4 end;      // end color rgba
    vec4 misc;     // x = lifeRemaining; y = lifeMax; z = seed/id; w = endSize
    vec4 extra;    // x = startSize; y = rotationSpeed; z = drag; w = unused
};

layout(std430, binding = 0) readonly buffer PartIn { Particle particles[]; };

uniform mat4 u_ViewProj;
uniform vec3 u_CamRight;
uniform vec3 u_CamUp;

out vec2 v_UV;
out vec4 v_PColor;

void main() {
    uint id = uint(gl_InstanceID);
    Particle p = particles[id];
    
    vec3 worldPos = p.pos.xyz;
    float size = p.pos.w;
    float rotation = p.vel.w;
    
    // Calculate life progress for color interpolation
    float lifeMax = max(p.misc.y, 0.0001);
    float t = 1.0 - (p.misc.x / lifeMax);
    
    // Interpolate color
    vec4 startColor = p.color;
    vec4 endColor = p.end;
    v_PColor = mix(startColor, endColor, t);
    
    // Apply rotation to quad
    float cosR = cos(rotation);
    float sinR = sin(rotation);
    vec2 rotatedQuad;
    rotatedQuad.x = a_quad.x * cosR - a_quad.y * sinR;
    rotatedQuad.y = a_quad.x * sinR + a_quad.y * cosR;
    
    vec2 quadPos = rotatedQuad * size;
    
    // Billboard towards camera
    vec3 offset = u_CamRight * quadPos.x + u_CamUp * quadPos.y;
    gl_Position = u_ViewProj * vec4(worldPos + offset, 1.0);
    
    // UV coordinates
    v_UV = a_quad + vec2(0.5);
}
