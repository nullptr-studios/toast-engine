#version 460 core

// Forward-shaded output
layout (location = 0) out vec4 gAlbedo;

in vec2 vTexCoord;
in vec3 vNormal;
in vec3 vWorldPos;

uniform vec4 gColor;
uniform sampler2D gTexture;
uniform sampler2D gDirectionalShadowMap;
uniform mat4 gDirectionalLightMatrix = mat4(1.0);
uniform vec3 gDirectionalLightDir = normalize(vec3(-0.35, -1.0, -0.25));
uniform float gDirectionalShadowBias = 0.0015;
uniform float gDirectionalShadowStrength = 0.8;
uniform int gDirectionalShadowsEnabled = 0;

float DirectionalShadowFactor(vec3 worldPos)
{
    if (gDirectionalShadowsEnabled == 0) {
        return 0.0;
    }

	vec4 lightClip = gDirectionalLightMatrix * vec4(worldPos, 1.0);
    vec3 proj = lightClip.xyz / max(lightClip.w, 1e-5);
    proj = proj * 0.5 + 0.5;

    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0 || proj.z <= 0.0 || proj.z > 1.0) {
        return 0.0;
    }

          vec2 texelSize = 1.0 / vec2(textureSize(gDirectionalShadowMap, 0));
    float currentDepth = proj.z - gDirectionalShadowBias;
    float occlusion = 0.0;

    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
                              float sampledDepth = texture(gDirectionalShadowMap, proj.xy + vec2(x, y) * texelSize).r;
            occlusion += currentDepth > sampledDepth ? 1.0 : 0.0;
        }
    }

    return (occlusion / 9.0) * clamp(gDirectionalShadowStrength, 0.0, 1.0);
}

void main()
{
    vec4 texel = texture(gTexture, vTexCoord) * gColor;
    if (texel.a < 0.1)
        discard;

    vec3 normal = normalize(vNormal);
    vec3 lightToSurface = normalize(-gDirectionalLightDir);
    float ndotl = max(dot(normal, lightToSurface), 0.0);
    float shadow = DirectionalShadowFactor(vWorldPos);

    float ambient = 0.35;
    float direct = ndotl * (1.0 - shadow);
    float lightFactor = ambient + direct;

    gAlbedo = vec4(texel.rgb * lightFactor, texel.a);
}