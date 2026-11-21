#version 460
#extension GL_EXT_ray_tracing : require
layout(set = 1, binding = 2, std140) uniform RTSecondary
{
    vec4 value;
}
secondaryData;
layout(location = 0) rayPayloadInEXT vec4 payload;
void main()
{
    payload = secondaryData.value;
}
