#version 440

layout(location = 0) in vec3 vNormal;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Block {
    mat4 mvp;
    vec4 lightDir;
} ubuf;

void main() {
    float d = max(dot(normalize(vNormal), normalize(ubuf.lightDir.xyz)), 0.0);
    vec3 base = vec3(0.62, 0.52, 0.95);
    fragColor = vec4(base * (0.22 + 0.78 * d), 1.0);
}
