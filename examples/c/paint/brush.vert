#version 450

layout(location = 0) in vec2 in_position; // Clip space
layout(location = 1) in vec2 in_local;    // -1..1 inside the brush square
layout(location = 2) in vec3 in_color;

layout(location = 0) out vec2 out_local;
layout(location = 1) out vec3 out_color;

void main() {
    gl_Position = vec4(in_position, 0.0, 1.0);
    out_local   = in_local;
    out_color   = in_color;
}