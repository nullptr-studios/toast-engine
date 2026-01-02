#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec4 aTangent; // xyz = tangent, w = handedness

uniform mat4 gMVP;
uniform mat4 gWorld;

out vec2 vTexCoord;
out vec3 vNormal;      // world space normal
out vec3 vTangent;     // world space tangent
out float vHandedness; // handedness sign
out vec3 vWorldPos;

void main()
{
    gl_Position = gMVP * vec4(aPos, 1.0);
    vTexCoord = aTexCoord;

    // Transform to world space. If you use non-uniform scale, replace with inverse-transpose normal matrix.
    vNormal = (gWorld * vec4(aNormal, 0.0)).xyz;

    // Forward tangent to the fragment shader
    vTangent = (gWorld * vec4(aTangent.xyz, 0.0)).xyz;
    vHandedness = aTangent.w;
    vWorldPos = (gWorld * vec4(aPos, 1.0)).xyz;
}