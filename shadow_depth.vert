#version 330 core

layout(location=0) in vec4 in_Position;

uniform mat4 myMatrix;
uniform mat4 lampLightSpace;

void main()
{
    vec4 worldPos = myMatrix * in_Position;
    gl_Position = lampLightSpace * worldPos;
}