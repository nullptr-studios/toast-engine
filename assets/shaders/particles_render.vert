#version 430 core

layout(location = 0) in vec2 a_quad; // unit quad (-0.5..0.5)

struct Particle {
    vec4 pos;
    vec4 vel;
    vec4 color;
    vec4 end;
    vec4 misc;
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
    vec2 quadPos = a_quad * size;

    vec3 offset = u_CamRight * quadPos.x + u_CamUp * quadPos.y;
    gl_Position = u_ViewProj * vec4(worldPos + offset, 1.0);
    v_UV = a_quad + vec2(0.5);
    v_PColor = p.color;
}
