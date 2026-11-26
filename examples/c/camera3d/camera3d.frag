#version 450

layout(set = 0, binding = 0) uniform Scene {
    mat4 view_projection;
    vec4 camera_position;
    vec4 fog_color;
    vec4 light_direction;
} scene;

layout(push_constant) uniform Object {
    mat4 model;
    vec4 color;
} object;

layout(location = 0) in vec3 in_world_position;
layout(location = 1) in vec3 in_normal;

layout(location = 0) out vec4 out_color;

// Colors are given in sRGB; the sRGB swapchain expects linear values
vec3 srgb_to_linear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

void main() {
    vec3 normal = normalize(in_normal);
    vec3 base   = srgb_to_linear(object.color.rgb);

    // Floor: darken every other 1x1 tile
    float tile = mod(floor(in_world_position.x) + floor(in_world_position.z), 2.0);
    base *= 1.0 - object.color.a * tile;

    // Simple shading so the faces of a box look different: some light from
    // the sky (more on faces pointing up) and one directional light. Real
    // lighting comes later in the roadmap.
    float sky     = 0.5 + 0.5 * normal.y;
    float diffuse = max(dot(normal, -normalize(scene.light_direction.xyz)), 0.0);
    vec3  lit     = base * (0.12 + 0.18 * sky + 0.75 * diffuse);

    // Fog: far objects fade into the background, which helps to see depth
    float distance = length(in_world_position - scene.camera_position.xyz);
    float fog      = 1.0 - exp(-pow(distance * scene.fog_color.a, 2.0));

    out_color = vec4(mix(lit, srgb_to_linear(scene.fog_color.rgb), fog), 1.0);
}