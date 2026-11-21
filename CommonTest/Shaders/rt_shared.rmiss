#version 460
#extension GL_EXT_ray_tracing : require
layout(set = 1, binding = 0, std140) uniform RTShared
{
    vec4 value;
}
sharedData;
layout(location = 0) rayPayloadInEXT vec4 payload;
void main()
{
    payload = sharedData.value;
}
