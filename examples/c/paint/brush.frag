#version 450

layout(location = 0) in vec2 in_local;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec4 out_color;

void main() {
    // Cut the square into a circle: drop pixels outside radius 1
    if (dot(in_local, in_local) > 1.0) {
        discard;
    }
    out_color = vec4(in_color, 1.0);
}