#version 430 core

in vec2 v_UV;
in vec4 v_PColor;

layout(binding = 1) uniform sampler2D u_Tex;

uniform int u_UseTexture;

out vec4 FragColor;

void main() {
    if (u_UseTexture != 0) {
        vec4 tex = texture(u_Tex, v_UV);
        FragColor = tex * v_PColor;
    } else {
        // No texture - just use color with a soft circular shape
        vec2 center = v_UV - vec2(0.5);
        float dist = length(center) * 2.0;
        float alpha = 1.0 - smoothstep(0.8, 1.0, dist);
        FragColor = vec4(v_PColor.rgb, v_PColor.a * alpha);
    }
    
    // Discard nearly transparent fragments
    if (FragColor.a < 0.01) {
        discard;
    }
}
