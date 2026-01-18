#version 430 core
in vec2 v_UV;
in vec4 v_PColor;
layout(binding = 1) uniform sampler2D u_Tex;
out vec4 FragColor;
void main() {
    vec4 tex = texture(u_Tex, v_UV);
    FragColor = tex * v_PColor;
    if (FragColor.a < 0.01) discard;
}
