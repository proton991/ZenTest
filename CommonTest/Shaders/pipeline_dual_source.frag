#version 450
layout(location = 0, index = 0) out vec4 color;
layout(location = 0, index = 1) out vec4 blend;
void main()
{
    color = vec4(1);
    blend = vec4(1, 0, 0, 1);
}
