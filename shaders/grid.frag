#version 330 core

in vec3 vColor;

uniform float uAlpha;

out vec4 FragColor;

void main()
{
    FragColor = vec4(vColor, uAlpha);
}
