#version 450

// Engine shader for the 3D renderer: compiled into the library at build time.

// Must match AT_RENDERER3D_MAX_*_LIGHTS in renderer3d.h
#define MAX_DIRECTIONAL_LIGHTS 4
#define MAX_POINT_LIGHTS       32
#define MAX_SPOT_LIGHTS        16

// Colors are linear and already multiplied by the light's intensity
struct DirectionalLight {
    vec4 direction; // xyz: where the light travels (unit)
    vec4 color;     // rgb
};

struct PointLight {
    vec4 position_range; // xyz: position, w: range
    vec4 color;          // rgb
};

struct SpotLight {
    vec4 position_range;      // xyz: position, w: range
    vec4 direction_cos_outer; // xyz: where it points (unit), w: cos(outer angle)
    vec4 color_cos_inner;     // rgb: color, w: cos(inner angle)
};

// Set 0: shared by every mesh of a pass
layout(set = 0, binding = 0) uniform Frame {
    mat4             view_projection;
    vec4             camera_position; // xyz
    vec4             ambient_sky;     // rgb: light on faces pointing up
    vec4             ambient_ground;  // rgb: light on faces pointing down
    uvec4            light_counts;    // x: directional, y: point, z: spot
    DirectionalLight directional[MAX_DIRECTIONAL_LIGHTS];
    PointLight       point[MAX_POINT_LIGHTS];
    SpotLight        spot[MAX_SPOT_LIGHTS];
} frame;

// Set 1: the material's texture (sRGB textures are read as linear colors)
layout(set = 1, binding = 0) uniform sampler2D base_texture;

layout(push_constant) uniform Object {
    mat4 model;
    vec4 base_color;
    vec4 emissive;
    vec4 params;
    vec4 uv_transform;
} object;

layout(location = 0) in vec3 in_world_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

// Colors are given in sRGB (like a color picker), but lighting works in
// linear space; the sRGB swapchain converts the result back.
vec3 srgb_to_linear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

// Blinn-Phong: diffuse light, brightest on faces turned toward the light,
// and a highlight around the direction halfway between the light and the
// camera. light is the light's color where it reaches the surface.
vec3 blinn_phong(vec3 normal, vec3 to_light, vec3 to_camera, vec3 light, vec3 base,
                 float specular, float shininess) {
    float diffuse = dot(normal, to_light);
    if (diffuse <= 0.0) {
        return vec3(0.0); // The light is behind the surface
    }

    vec3  halfway   = normalize(to_light + to_camera);
    float highlight = pow(max(dot(normal, halfway), 0.0), shininess);
    return light * (base * diffuse + specular * highlight);
}

// How much of a point or spot light reaches this distance: it fades with
// the square of the distance, and smoothly reaches zero at the range
float distance_falloff(float distance, float range) {
    float ratio  = distance / range;
    float window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
    return window * window / (distance * distance + 1.0);
}

void main() {
    float specular     = object.params.x;
    float shininess    = object.params.y;
    float alpha_cutoff = object.params.z;
    bool  unlit        = object.params.w > 0.5;

    // Surface color: the material color times its texture
    vec4 base = vec4(srgb_to_linear(object.base_color.rgb), object.base_color.a)
              * texture(base_texture, in_uv);

    // Holes (fences, leaves): these pixels are not drawn at all
    if (base.a < alpha_cutoff) {
        discard;
    }

    vec3 emissive = srgb_to_linear(object.emissive.rgb) * object.emissive.w;

    if (unlit) {
        out_color = vec4(base.rgb + emissive, base.a);
        return;
    }

    // Double-sided materials: the back face is lit as a surface facing us
    vec3 normal = normalize(in_normal);
    if (!gl_FrontFacing) {
        normal = -normal;
    }
    vec3 to_camera = normalize(frame.camera_position.xyz - in_world_position);

    // Ambient: from the sky on faces pointing up, from the ground on faces
    // pointing down, blended in between
    vec3 color = base.rgb * mix(frame.ambient_ground.rgb, frame.ambient_sky.rgb,
                                0.5 + 0.5 * normal.y);

    // Directional lights: the same everywhere
    for (uint i = 0u; i < frame.light_counts.x; ++i) {
        DirectionalLight light = frame.directional[i];
        color += blinn_phong(normal, -light.direction.xyz, to_camera, light.color.rgb,
                             base.rgb, specular, shininess);
    }

    // Point lights: fade with the distance
    for (uint i = 0u; i < frame.light_counts.y; ++i) {
        PointLight light    = frame.point[i];
        vec3       to_light = light.position_range.xyz - in_world_position;
        float      distance = length(to_light);
        if (distance >= light.position_range.w || distance < 1e-4) {
            continue;
        }

        vec3 reaching = light.color.rgb * distance_falloff(distance, light.position_range.w);
        color += blinn_phong(normal, to_light / distance, to_camera, reaching,
                             base.rgb, specular, shininess);
    }

    // Spot lights: fade with the distance, and from the inner edge of the
    // cone to the outer one
    for (uint i = 0u; i < frame.light_counts.z; ++i) {
        SpotLight light    = frame.spot[i];
        vec3      to_light = light.position_range.xyz - in_world_position;
        float     distance = length(to_light);
        if (distance >= light.position_range.w || distance < 1e-4) {
            continue;
        }
        to_light /= distance;

        float cos_angle = dot(-to_light, light.direction_cos_outer.xyz);
        float cone      = smoothstep(light.direction_cos_outer.w, light.color_cos_inner.w,
                                     cos_angle);
        if (cone <= 0.0) {
            continue;
        }

        vec3 reaching = light.color_cos_inner.rgb * cone
                      * distance_falloff(distance, light.position_range.w);
        color += blinn_phong(normal, to_light, to_camera, reaching,
                             base.rgb, specular, shininess);
    }

    out_color = vec4(color + emissive, base.a);
}