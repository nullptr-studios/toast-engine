#version 460 core

layout (location = 0) out vec4 oFragColor;

in vec2 vTexCoord;

uniform sampler2D gAlbedoTexture;
uniform sampler2D gLightingTexture;

void main()
{
    oFragColor = texture(gAlbedoTexture, vTexCoord) * texture(gLightingTexture, vTexCoord);
}