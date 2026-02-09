#version 460

layout (location = 0) out vec4 oLightingAcum;

uniform vec4 gLightColor;

uniform float gLightIntensity;
uniform float gLightVolumetricIntensity;
uniform float gLightAngle;

uniform float gRadialSoftness;
uniform float gAngularSoftness;

uniform bool gNormalMappingEnabled;

uniform sampler2D gNormal;

uniform vec2 gInvScreenSize;

in vec2 vTexCoord;

void main() {
    // Sample the g-buffer at the exact screen pixel corresponding to this fragment
    vec2 screenUV = gl_FragCoord.xy * gInvScreenSize;

    // Light is centered at quad UV = (0.5, 0.5)
    vec2 center = vec2(0.5);
    vec2 local = vTexCoord - center;

    float maxDist = 0.70710678118; // precalculated sqrt(0.5)
    float d = length(local) / maxDist;
    
    // radial attenuation
    float radialFallof = pow(1.0 - d, 2.0);
    radialFallof = smoothstep(.70f, .70f - gRadialSoftness, d);

    // angular attenuation
    float angularFallof = 1.0;
    float angle = atan(local.y, local.x);
    
    angularFallof = smoothstep(gLightAngle + gAngularSoftness, gLightAngle, abs(angle));
    
    float attenuation = radialFallof * angularFallof;

    vec3 finalColor = gLightColor.rgb * gLightIntensity * attenuation;
    finalColor += gLightColor.rgb * gLightVolumetricIntensity * attenuation;
    
    // apply normal lighting (conditionally)
    float normalFactor = 1.0;
    
    if (gNormalMappingEnabled) {
        vec4 normalSample = texture(gNormal, screenUV);
        vec2 normal = normalSample.xy * 2.0 - 1.0;
        float nLen = length(normal);
        normal /= nLen;

        vec2 dirtoLight = center - vTexCoord;
        float lLen = length(dirtoLight);
        dirtoLight /= lLen;

        float NdotL = dot(normal, dirtoLight);
        NdotL = clamp(NdotL, 0.0, 1.0);

        // Blend out the normal effect as the cone becomes 180°
        float pi = 3.14159265359;
        // at >= 180° (no normal shading)
        float omniFactor = clamp(gLightAngle / pi, 0.0, 1.0);
        normalFactor = mix(NdotL, 1.0, omniFactor);
    }

    finalColor *= normalFactor;
    
    oLightingAcum = vec4(finalColor, 1.0);
}
