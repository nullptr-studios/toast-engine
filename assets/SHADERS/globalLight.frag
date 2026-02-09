#version 460

layout(location = 0) out vec4 oGlobalLightColor;

uniform float gLightIntensity;
uniform vec3 gLightColor;


in vec2 fTexCoord;

void main() {
    
    oGlobalLightColor = vec4(clamp(vec3(gLightIntensity), 0.0, 2.0), 1.0) * vec4(gLightColor, 1.0);
}