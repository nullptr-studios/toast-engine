#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec4 aColor;

uniform mat4 transform;

out vec2 TexCoord;
out vec4 VertexColor;

void main()
{
    gl_Position = transform * vec4(aPos, 1.0);
    
    // flip image on the y axis
    TexCoord = vec2(aTexCoord.x, aTexCoord.y) * vec2(1.0, -1.0) + vec2(0.0, 1.0);
    VertexColor = aColor;
}