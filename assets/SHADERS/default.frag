#version 460 core

// G-buffer outputs: albedo, world position, world normal
layout (location = 0) out vec4 gAlbedo;
// layout (location = 1) out vec4 gPosition;

in vec2 vTexCoord;
in vec3 vNormal;
in vec3 vTangent;
in float vHandedness;
in vec3 vWorldPos;

uniform vec4 gColor;
uniform sampler2D gTexture;
uniform sampler2D gNormalMap; // tangent-space normal map

vec3 unpackNormal(vec3 n) {
    return normalize(n * 2.0 - 1.0);
}

void main()
{
    vec4 texel = texture(gTexture, vTexCoord) * gColor;
    if (texel.a < 0.1)
        discard;

    gAlbedo   = texel;
    // gPosition = vec4(vWorldPos, 1.0);
}