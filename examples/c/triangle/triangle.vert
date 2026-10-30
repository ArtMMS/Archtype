#version 450

// Triangle written directly in clip space (no vertex buffer yet).
// Vulkan's Y axis points down: y = -0.5 is above the center.
// Listed counter-clockwise as seen on screen, so it is a front face.
const vec2 positions[3] = vec2[](
    vec2( 0.0, -0.5),   // top
    vec2(-0.5,  0.5),   // bottom left
    vec2( 0.5,  0.5)    // bottom right
);

const vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),   // red
    vec3(0.0, 0.0, 1.0),   // blue
    vec3(0.0, 1.0, 0.0)    // green
);

layout(location = 0) out vec3 out_color;

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    out_color   = colors[gl_VertexIndex];
}