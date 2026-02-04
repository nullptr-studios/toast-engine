#version 460 core
out vec4 FragColor;

//in vec2 TexCoord;

void main(void)
{
    vec2 resolution = vec2(800.0f, 800.f);
    vec2 TexCoord = gl_FragCoord.st / resolution.xy;
	
    float x = floor(TexCoord.x * 8.0);
    float y = floor(TexCoord.y * 8.0);
    float pattern = mod(x + y, 2.0);
    vec3 color = mix(vec3(.5f,0,.5f), vec3(0.0), pattern);
    FragColor = vec4(color, 1.0);
}