#version 460

layout (location = 0) out vec4 oLightingAcum;

uniform vec4 gLightColor;

uniform float gLightIntensity;
uniform float gLightVolumetricIntensity;
uniform float gLightAngle;

uniform float gRadialSoftness;
uniform float gAngularSoftness;

in vec2 vTexCoord;

void main() {
    // Light is centered at quad UV = (0.5, 0.5)
    vec2 center = vec2(0.5);
    vec2 local = vTexCoord - center;

    float maxDist = 0.70710678118; // precalculated sqrt(0.5)
    float d = length(local) / maxDist;
    
    // radial attenuation
    float radialFallof = smoothstep(.70f, .70f - gRadialSoftness, d);

    // angular attenuation
    float angularFallof = 1.0;
    float angle = atan(local.y, local.x);
    
    angularFallof = smoothstep(gLightAngle + gAngularSoftness, gLightAngle, abs(angle));
    
    float attenuation = radialFallof * angularFallof;

    vec3 finalColor = gLightColor.rgb * gLightIntensity * attenuation;
    finalColor += gLightColor.rgb * gLightVolumetricIntensity * attenuation;
    
    oLightingAcum = vec4(finalColor, 1.0);
}
