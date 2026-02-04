#version 460 core

// G-buffer outputs: albedo, world position, world normal
layout (location = 0) out vec4 gAlbedo;
// layout (location = 1) out vec4 gPosition;
layout (location = 1) out vec4 gNormal;

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

    // Normalize geometry normal and tangent
    vec3 N = normalize(vNormal);
    vec3 T = vTangent;
    T = normalize(T - N * dot(N, T)); // Gram\-Schmidt
    vec3 B = cross(N, T) * vHandedness;

    // Sample normal map and transform to world space
    vec3 tangentNormal = unpackNormal(texture(gNormalMap, vTexCoord).rgb);
    mat3 TBN = mat3(T, B, N);
    vec3 worldNormal = normalize(TBN * tangentNormal);

    gAlbedo   = texel;
    // gPosition = vec4(vWorldPos, 1.0);
    gNormal   = vec4(worldNormal, texel.a);
}