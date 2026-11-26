#version 450

// Engine shader for the 2D renderer: compiled into the library at build time.

// Shapes use a 1x1 white texture, so the result is just the vertex color
layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_color;

layout(location = 0) out vec4 out_color;

void main() {
    out_color = texture(tex, in_uv) * in_color;
}