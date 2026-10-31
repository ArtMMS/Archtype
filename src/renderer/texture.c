#include "archtype/renderer/texture.h"
#include "renderer/renderer_internal.h"

#include <stdlib.h>
#include <string.h>

#include "renderer/image_loader.h"

static VkFilter to_vk_filter(at_texture_filter filter) {
    return filter == AT_TEXTURE_FILTER_NEAREST ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

static VkSamplerAddressMode to_vk_address_mode(at_texture_wrap wrap) {
    switch (wrap) {
        case AT_TEXTURE_WRAP_CLAMP:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case AT_TEXTURE_WRAP_MIRROR: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case AT_TEXTURE_WRAP_REPEAT:
        default:                     return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

/* sRGB textures are converted to linear when sampled, so shader math  */
/* (lighting, blending) happens in linear space, as it should.         */
static VkFormat to_vk_format(at_texture_color_space color_space) {
    return color_space == AT_TEXTURE_COLOR_SPACE_LINEAR ? VK_FORMAT_R8G8B8A8_UNORM
                                                        : VK_FORMAT_R8G8B8A8_SRGB;
}

static bool create_sampler(at_renderer *renderer, at_texture *texture,
                           const at_texture_options *options) {
    VkFilter             filter  = to_vk_filter(options->filter);
    VkSamplerAddressMode address = to_vk_address_mode(options->wrap);

    VkSamplerCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter    = filter;
    info.minFilter    = filter;
    info.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = address;
    info.addressModeV = address;
    info.addressModeW = address;
    info.minLod       = 0.0f;
    info.maxLod       = 0.0f; /* No mipmaps yet */
    info.borderColor  = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

    VkResult result = vkCreateSampler(renderer->context.device, &info, NULL, &texture->sampler);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkCreateSampler", result);
        return false;
    }
    return true;
}

at_texture *at_texture_create(at_renderer *renderer, uint32_t width, uint32_t height,
                              const void *pixels, const at_texture_options *options) {
    if (!renderer || !pixels || width == 0 || height == 0) {
        fprintf(stderr, "[archtype] Invalid texture (pixels and a size are required)\n");
        return NULL;
    }

    VkPhysicalDeviceLimits *limits = &renderer->context.physical_device_properties.limits;
    if (width > limits->maxImageDimension2D || height > limits->maxImageDimension2D) {
        fprintf(stderr, "[archtype] Texture %ux%u is larger than the GPU limit (%u)\n",
                width, height, limits->maxImageDimension2D);
        return NULL;
    }

    at_texture_options defaults;
    memset(&defaults, 0, sizeof(defaults));
    if (!options) {
        options = &defaults;
    }

    at_texture *texture = (at_texture *)calloc(1, sizeof(*texture));
    if (!texture) {
        fprintf(stderr, "[archtype] Failed to allocate texture\n");
        return NULL;
    }

    bool ok = at_vk_image_create(&texture->image, renderer->allocator, &renderer->context,
                                 width, height, to_vk_format(options->color_space),
                                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
           && at_vk_image_upload(&texture->image, renderer->allocator, &renderer->upload,
                                 &renderer->context, pixels)
           && create_sampler(renderer, texture, options);

    if (!ok) {
        at_texture_destroy(renderer, texture);
        return NULL;
    }

    return texture;
}

at_texture *at_texture_load(at_renderer *renderer, const char *path,
                            const at_texture_options *options) {
    if (!renderer || !path) {
        return NULL;
    }

    at_image_data image;
    if (!at_image_load(path, &image)) {
        return NULL;
    }

    at_texture *texture = at_texture_create(renderer, image.width, image.height,
                                            image.pixels, options);
    if (texture) {
        fprintf(stderr, "[archtype] Texture loaded: %s (%ux%u)\n",
                path, image.width, image.height);
    }

    at_image_free(&image);
    return texture;
}

void at_texture_get_size(const at_texture *texture, uint32_t *width, uint32_t *height) {
    if (width) {
        *width = texture->image.extent.width;
    }
    if (height) {
        *height = texture->image.extent.height;
    }
}

void at_texture_destroy(at_renderer *renderer, at_texture *texture) {
    if (!renderer || !texture) {
        return;
    }

    /* The GPU may still be sampling this texture */
    vkDeviceWaitIdle(renderer->context.device);

    if (texture->sampler != VK_NULL_HANDLE) {
        vkDestroySampler(renderer->context.device, texture->sampler, NULL);
    }
    at_vk_image_destroy(&texture->image, renderer->allocator, &renderer->context);
    free(texture);
}