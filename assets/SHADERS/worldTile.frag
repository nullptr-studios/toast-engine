#version 460 core

// Forward-shaded output
layout (location = 0) out vec4 gAlbedo;

in vec2 vTexCoord;
in vec3 vWorldPos;

uniform vec4 gColor;
uniform sampler2D gTexture;
uniform float gTextureSize;

void main()
{
    vec2 worldUV = vWorldPos.xy / gTextureSize;

    vec4 texel = texture(gTexture, worldUV) * gColor;
    if (texel.a < 0.1)
        discard;

    gAlbedo = texel;
}

