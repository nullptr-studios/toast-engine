#version 460 core

layout (location = 0) out vec4 oFragColor;

in vec2 vTexCoord;

uniform sampler2D gAlbedoTexture;
uniform sampler2D gLightingTexture;

// Global (ambient) light added on top of accumulated lighting
uniform float gGlobalLightIntensity;
uniform vec3 gGlobalLightColor;

void main()
{
    vec4 albedo = texture(gAlbedoTexture, vTexCoord);
    vec4 lighting = texture(gLightingTexture, vTexCoord);
    vec3 global = gGlobalLightColor * gGlobalLightIntensity;

    // Combine: albedo modulated by (accumulated lighting + global light)
    vec3 finalLighting = lighting.rgb + global;
    oFragColor = vec4(albedo.rgb * finalLighting, albedo.a);
}