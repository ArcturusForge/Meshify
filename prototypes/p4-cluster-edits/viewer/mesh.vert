#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec3 vNormal;

layout(std140, binding = 0) uniform Block {
    mat4 mvp;
    vec4 lightDir;
} ubuf;

void main() {
    vNormal = normal;
    gl_Position = ubuf.mvp * vec4(position, 1.0);
}
