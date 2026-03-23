#version 460

layout (location = 0) out vec4 oLightingAcum;

in vec2 vTexCoord;

// Light parameters
uniform vec4 gLightColor;
uniform float gLightIntensity;
uniform float gLightVolumetricIntensity;
uniform float gLightAngle;
uniform vec2 gLightDirection;
uniform float gRadialSoftness;
uniform float gAngularSoftness;

// Screen-space SDF
uniform vec2 lightPosUV;
uniform float lightRadius;
uniform int gShadowSteps;
uniform ivec2 screenSize;
uniform sampler2D sdfTex;

void main()
{
	// Convert fragment position to screen UV (0-1 range)
	vec2 screenUV = gl_FragCoord.xy / vec2(screenSize);

	// ---------------------------
	// 1. Light local coordinates in screen space
	// ---------------------------
	vec2 local = screenUV - lightPosUV;
	float distToLight = length(local);

	if(distToLight > lightRadius) {
		discard; // outside light radius
	}

	// ---------------------------
	// 2. Radial attenuation
	// ---------------------------
	float dNorm = distToLight / lightRadius;
	float radialFalloff = smoothstep(0.7, 0.7 - gRadialSoftness, dNorm);

	// ---------------------------
	// 3. Angular attenuation (cone direction)
	// ---------------------------
	float angle = atan(local.y, local.x);
	float directionAngle = atan(gLightDirection.y, gLightDirection.x);
	float delta = abs(atan(sin(angle - directionAngle), cos(angle - directionAngle)));
	float angularFalloff = smoothstep(gLightAngle + gAngularSoftness, gLightAngle, delta);

	float attenuation = radialFalloff * angularFalloff;

	// ---------------------------
	// 4. Shadow raymarch (SDF in screen space)
	// ---------------------------
	vec2 rayPos = lightPosUV;
	float shadow = 1.0;
	float rayStep = lightRadius / float(gShadowSteps);

	for(int i = 0; i < gShadowSteps; ++i) {
		rayPos += rayDir * rayStep;
		float rayLen = length(rayPos - lightPosUV);

		// Stop marching if we've gone past the light radius
		if(rayLen >= lightRadius) break;

		// Sample SDF: tells us distance to nearest occluder
		float sdfDist = texture(sdfTex, rayPos).r;

		// Contact shadow: if SDF is small, occluder is near this ray position
		// Soft penumbra based on how close we are to geometry
		float contactShadow = smoothstep(rayStep * 2.0, 0.0, sdfDist);
		shadow = min(shadow, 1.0 - contactShadow * 0.9);

		// Early exit if fully in shadow
		if(shadow < 0.01) break;
	}

	// ---------------------------
	// 5. Combine color
	// ---------------------------
	vec3 finalColor = gLightColor.rgb * gLightIntensity * attenuation * shadow;
	finalColor += gLightColor.rgb * gLightVolumetricIntensity * attenuation * shadow;

	oLightingAcum = vec4(finalColor, attenuation * shadow);
}
