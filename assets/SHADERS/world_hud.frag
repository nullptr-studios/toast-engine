#version 460 core

layout (location = 0) out vec4 gAlbedo;

in vec2 vTexCoord;

uniform vec4 gColor;
uniform sampler2D gTexture;

void main()
{
    vec2 uv = vec2(vTexCoord.x, 1.0 - vTexCoord.y);
    vec4 texel = texture(gTexture, uv) * gColor;
    if (texel.a < 0.01)
        discard;

    gAlbedo = texel;
}

