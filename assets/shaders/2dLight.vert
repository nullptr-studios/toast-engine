#version 460

layout (location = 0) in vec3 aPos;
layout (location = 2) in vec2 aTexCoord;

uniform mat4 gMVP;

out vec2 vTexCoord;

void main() {
    gl_Position = gMVP * vec4(aPos, 1.0);
    
    vTexCoord = aTexCoord;
}