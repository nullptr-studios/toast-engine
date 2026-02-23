#version 460 core
out vec4 FragColor;

in vec2 TexCoord;
in vec4 VertexColor;

uniform sampler2D Texture;

void main()
{
    vec4 texel = texture(Texture, TexCoord) * VertexColor;
    // Discard only fully invisible fragments; let GPU blending handle semi-transparency
    if (texel.a < 0.004f)
        discard;
    FragColor = texel;
}