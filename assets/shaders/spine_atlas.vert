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
    
    TexCoord = aTexCoord;
    VertexColor = aColor;
}